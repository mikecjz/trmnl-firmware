# Xteink X4 Classic — recovery and rollback

> [!WARNING]
> **`merged_firmware.bin` at `0x0` is stock→TRMNL *conversion only*.**
> That image is the one-shot conversion from stock Xteink firmware to the TRMNL
> layout. It carries the bootloader, the partition table and the app, and
> `esptool merge_bin` fills every gap between them with `0xFF` — so writing it
> at `0x0` also **erases NVS at `0xe000`** (Wi-Fi credentials, API key, saved
> `api_url`) and **`otadata`**.
>
> On a unit that already runs the TRMNL layout — the normal case for every
> update after the first one — the only correct write is **app-only**:
> `firmware.bin` at **`0x20000`** (see §3). Reflashing the merged image over a
> provisioned unit drops it back into its `TRMNL-xxxxxx` setup portal and loses
> its saved server URL and key. The server-side registration is keyed by MAC and
> is not affected, so the unit re-registers once it is pointed at the right
> server again.

This procedure is required before the first write and again whenever a build
needs to be undone. It works entirely through the ESP32-S3 ROM USB bootloader,
which is mask ROM: a bad application image cannot remove it.

The device's ROM loader enumerates as **`303A:1001`**. The stock Xteink firmware
enumerates as `303A:4002` (USB-MSC); the TRMNL build enumerates as USB-CDC. That
difference is expected and only affects the serial port name, not recovery.

## 1. Enter ROM download mode

1. Power the device fully off (hold the power key until the panel clears, then
   release; power is GPIO3 on this board).
2. Hold **UP** (the side key that CrossPoint calls previous) while plugging in a
   USB **data** cable. Keep holding for about two seconds after the port appears.
3. Check the host:

   ```sh
   ls /dev/cu.usbmodem*            # macOS
   ls /dev/ttyACM*                 # Linux
   system_profiler SPUSBDataType | grep -i 303a
   ```

If nothing appears, try another cable and port; charge-only cables are the most
common cause. Do not keep retrying writes on a port that is not the ROM loader.
If the ROM port appears while the stock firmware is still installed, the device
is in download mode and nothing has been written yet.

## 2. Confirm the port and take the full backup

```sh
PORT=/dev/cu.usbmodemXXXX        # substitute the port you actually see
esptool.py --chip esp32s3 --port "$PORT" chip_id
esptool.py --chip esp32s3 --port "$PORT" read_flash 0x0 0x1000000 x4c-stock-backup.bin
shasum -a 256 x4c-stock-backup.bin
```

The expected size is exactly `16777216` bytes (16 MiB). Keep the file and its
SHA-256 off the device. The dump includes the stock bootloader, partition table,
NVS (calibration data) and the stock app — this is the only guaranteed way back
to stock, because the TRMNL partition layout is different.

### Verified backup for this unit

Taken read-only from the unit's ROM loader on 2026-11-22 and verified by
`esptool verify-flash` over all 16 MiB:

| Field | Value |
|---|---|
| File (runtime host) | `/Users/junzhouchen/trmnl-x4c-backup/x4c-stock-16MB.bin` |
| Size | `16777216` bytes (`0x1000000`) |
| SHA-256 | `907c752a21dafc00863890d613ccf8c165b431f9014cd40c3446255593f006b2` |
| Device | ESP32-S3 rev v0.2, MAC `f8:5b:1b:95:36:ec`, 16 MB flash |
| Per-chunk hashes | `/Users/junzhouchen/trmnl-x4c-backup/MANIFEST.txt` |

The stub flasher aborts with "Serial data stream stopped" on this unit's USB
link; the ROM loader (`--no-stub`) completes reliably at ~300 kbit/s. Use it for
both reads and writes here.

Keep a second copy outside that host (the file is the only record of the stock
NVS, including Wi-Fi calibration).

## 3. Flash the TRMNL merged image

Only after the backup exists and its hash has been recorded:

**Conversion only — this write destroys NVS and otadata (warning at the top of
this document).** Use it once, while the unit still runs stock Xteink firmware.
For any later update of a converted unit, skip to the app-only command below.

```sh
esptool.py --chip esp32s3 --port "$PORT" write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  -z 0x0 merged_firmware.bin
```

`merged_firmware.bin` is produced by the build at
`.pio/build/xteink_x4_classic/merged_firmware.bin` and contains the bootloader at
`0x0`, the partition table at `0x8000` and the app at `0x20000`.

App-only updates, once the TRMNL layout is on the device, use `firmware.bin` at
the app offset **(0x20000, not 0x10000)**:

```sh
esptool --chip esp32s3 --port "$PORT" --no-stub write-flash -z 0x20000 firmware.bin
```

### App-only on the stock layout (bring-up)

The stock partition table puts `app0` at **0x10000** and `otadata` selects it.
While that table is still on the device (no merged image written yet), a
bring-up app can be flashed over the stock app without touching the bootloader,
partition table, NVS or the backup:

```sh
esptool --chip esp32s3 --port "$PORT" --no-stub write-flash 0x10000 firmware.bin
# read it back and compare before trusting the boot
esptool --chip esp32s3 --port "$PORT" --no-stub read-flash 0x10000 "$(stat -f%z firmware.bin)" readback.bin
shasum -a 256 firmware.bin readback.bin
```

Never write `firmware.bin` over the stock partition layout at the TRMNL offset:
the stock app lives at 0x10000 and the device will not boot.

## 4. Monitor

```sh
../.venv/bin/pio device monitor -b 115200
```

or any 115200 8N1 serial monitor. The X4C boot log prints the panel probe result
(`X4C panel: VER=.. .. .. -> ...`), then Wi-Fi/setup information.

## 5. Rollback to stock

Enter download mode again and restore the whole chip. Verify the image hash
first, then write it — one pass, nothing else:

```sh
PORT=/dev/cu.usbmodemXXXX
BACKUP=/Users/junzhouchen/trmnl-x4c-backup/x4c-stock-16MB.bin
shasum -a 256 "$BACKUP"
# must print: 907c752a21dafc00863890d613ccf8c165b431f9014cd40c3446255593f006b2

esptool --chip esp32s3 --port "$PORT" --no-stub write-flash \
  --flash-mode dio --flash-freq 80m --flash-size 16MB \
  0x0 "$BACKUP"
```

`--no-stub` is required on this unit (see §2). The write restores the stock
firmware, bootloader, partition table and NVS in one pass. After it returns, the
device should enumerate as `303A:4002` (stock USB-MSC) on the next normal boot.

Do **not** run `erase_flash` first unless the full restore fails repeatedly; a
plain full-image write is enough and keeps the operation short. If the device is
wedged:

1. Hold UP and replug USB; the ROM loader (`303A:1001`) should still appear.
2. Re-run the backup restore at a lower baud rate (`--baud 115200`).
3. Only if the ROM loader is unreachable, try `esptool.py --chip esp32s3 erase_flash`
   and restore the backup again.

## 6. Guard rails

- Keep `x4c-stock-backup.bin` and its SHA-256 outside the repo and outside the
  build tree.
- Never interrupt a `write_flash` or the full-image restore.
- Do not change eFuses, secure boot or flash encryption for this bring-up.
- The backup can contain the unit's MAC and calibration data; treat it as
  private.
- Panel controller differences (SSD1677 / UC8179 / UC8279) are software-selected;
  no recovery step depends on knowing which one the unit has.

## 7. Setup portal (Wi-Fi and server)

After the client boots with no saved network it broadcasts an open AP,
**`TRMNL-<MAC suffix>`** (for this unit `TRMNL-9536EC`), on channel 6. The
portal is at **`http://4.3.2.1`** (not the usual 192.168.4.1) and stays open for
15 minutes.

Order matters:

1. **Advanced Settings -> Server -> "API server"** first, and press **Save**.
   The URL is stored immediately in NVS and does not depend on Wi-Fi. Enter it
   without a trailing slash (`https://oraclesj.mikeximin.com/kindle-trmnl`).
2. Then **Wi-Fi -> Select a Network**, pick the SSID, enter the password,
   **Connect**.
3. When it joins, the device pairs via `/api/setup` and fetches `/api/display`.

Notes:

- The ESP32-S3 is **2.4 GHz only**. A 5 GHz-only SSID never appears in the
  scan; typing one manually fails as "network not found". Use the 2.4 GHz SSID
  (or a band-steering SSID that offers 2.4 GHz) and WPA2-PSK where possible.
  Hidden SSIDs work through the "Network name" field.
- The Wi-Fi row's "No Wi-Fi saved yet" description means the portal has no saved
  network yet; it is not a live radio-status indicator.
- With the current client, the join attempt briefly takes the AP down (the STA
  path switched the radio to STA-only). A failed join restarts the device, and
  the AP returns about 20 s later with no network saved. If the AP does not come
  back: power-cycle (long-press power, then press to turn on). Still nothing:
  hold **UP** and plug USB to reach the ROM loader.
- The flow fix keeps the AP up during the attempt (AP+STA) and keeps the portal
  open after a failed join or a successful join with no Server saved, so the
  unit never needs a restart to be recovered. It is built but must be flashed
  as an app-only update at `0x20000` once the unit is back on USB.

## 8. Bring-up tools

- `tools/x4c-probe/` — standalone app-only probe. Powers the panel rail, resets
  the panel, issues only `0x70` and clocks three bytes back; no panel init, no
  LUT/waveform write. Flash at `0x10000` on the stock layout and read the
  `PROBE_RESULT` lines.
- `pio run -e x4c_panel_test` — panel-only test image: display init through the
  shipping driver path, an 800×480 alignment pattern (4 px frame, corner
  squares, rulers, diagonals, key labels), one full refresh, then key polling on
  GPIO0/GPIO7 over the console.

Verified on this unit: `0x70` returns `VER = 00 00 02`, i.e. **UC8279**
(id `0x02`), stable across repeated probes. The stock app never persists this
value, so an ejected-flash dump cannot reveal it — probe the panel instead.
