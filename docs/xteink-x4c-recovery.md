# Xteink X4 Classic — recovery and rollback

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

## 3. Flash the TRMNL merged image

Only after the backup exists and its hash has been recorded:

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
esptool.py --chip esp32s3 --port "$PORT" write_flash -z 0x20000 firmware.bin
```

Never write `firmware.bin` over the stock partition layout: the stock app lives
at a different offset and the device will not boot.

## 4. Monitor

```sh
../.venv/bin/pio device monitor -b 115200
```

or any 115200 8N1 serial monitor. The X4C boot log prints the panel probe result
(`X4C panel: VER=.. .. .. -> ...`), then Wi-Fi/setup information.

## 5. Rollback to stock

Enter download mode again and restore the whole chip:

```sh
esptool.py --chip esp32s3 --port "$PORT" write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  -z 0x0 x4c-stock-backup.bin
```

If the backup is from this exact unit, this restores the stock firmware,
bootloader, partition table and NVS in one pass. Do **not** run `erase_flash`
first unless the full restore fails repeatedly; a plain full-image write is
enough and keeps the operation short. If the device is wedged:

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
