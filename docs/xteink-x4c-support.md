# Xteink X4 Classic (X4 v2) — board notes for the TRMNL firmware

Provenance: this file captures what was extracted from the two existing X4C
implementations before writing any TRMNL board code.

* CrossPoint `x4c` (`crosspoint-reader/crosspoint-reader`, env `x4c`, PR #3279,
  merged 2026-08-29) and the `freeink-sdk` submodule it builds against:
  `freeink-sdk/docs/xteink-x4c-support.md` and the `XTEINK_X4_CLASSIC` board
  profile in `freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h`.
* Papyrix `x4c` (`bigbag/papyrix-reader`, env `x4c`): the
  `kX4ClassicProfile` in `lib/BoardSupport/src/BoardProfiles.cpp`.

Both are MIT-licensed; this repo is GPLv3, so borrowing the data is compatible
with attribution (CrossPoint notes the pinout was reverse-engineered from the
stock X4C flash, app `xteink_app v7.0.11`, board tag `ESP32S3_X4_CLA`).

## Agreed hardware

| Item | Value |
|---|---|
| SoC | ESP32-S3, 16 MB flash, 8 MB PSRAM (N16R8) |
| Display | 800x480 1-bit SPI e-paper, 4.28" class |
| Panel controllers | SSD1677 / UC8179 / UC8279, varies per production unit |
| Display SPI | SCLK 12, MOSI 11 (write-only, no MISO), CS 13, DC 14, RST 10, BUSY 18 |
| Display rail | GPIO1 — master peripheral/panel rail, HIGH at boot |
| Buttons | 7 discrete active-low GPIOs (no ADC ladder, no touch) |
| Power button | GPIO3 |
| Side keys | GPIO0, GPIO7 (page turn) |
| Bottom keys | GPIO5 (left), GPIO2 (right), GPIO8 (confirm), GPIO9 (back) |
| Battery gauge | CW2017 at I2C 0x63 (shared bus SDA 39 / SCL 38 @ 400 kHz) |
| Charge status | GPIO21 `STAT`, active HIGH |
| RTC | BM8563 at I2C 0x51 |
| IMU | QMI8658 at I2C 0x6B |
| SD card | native SDMMC 1-bit: CLK 41, CMD 42, DAT0 40; power enable GPIO6 (active low) |
| USB | native ESP32-S3 USB-OTG, D- 19 / D+ 20 |

## What was changed in the TRMNL firmware

* `platformio.ini`: new `[env:xteink_x4_classic]` on `esp32s3_n16r8` with the
  pioarduino 55.03.37 platform and the 16 MB `boards/trmnl_x_partitions.csv`
  (dual OTA). It is the SPI/`bb_epaper` path (no `PARALLEL_EPD`, unlike
  `TRMNL_X`). It carries the same `board_build.embed_txtfiles` cert list as the
  other S3 SPI env (`seeed_sticky`); without it the IDF build graph generates a
  `https_server.crt.S` node with a doubled build path and fails in SCons. The
  env is fully standalone rather than `extends = env:esp32_base` for the same
  reason: the SPI S3 envs are the supported shape here.
* `.github/workflows/x4c-build.yml`: fork CI that builds both `TRMNL_X` (the
  stock S3 target) and `xteink_x4_classic` and uploads each env's
  `firmware.bin` / `bootloader.bin` / `partitions.bin` as artifacts.
* `src/board_config.cpp`: device row `xteink_x4_classic` with the SPI pins
  above, `interrupt_pin` 3 (power button), `BATT_NONE`, placeholder panel set.
* `src/sleep_session.cpp`: hold GPIO1 HIGH across deep sleep on top of the
  existing power-button (`interrupt_pin` = 3) wake. GPIO1 is treated as the
  board's power latch, matching Papyrix's `PowerConfig{1, true}` and CrossPoint's
  master rail.
* `src/x4c_panel.cpp` / `include/x4c_panel.h`: factory/`0x70`/default panel
  resolution, the three `dpList` entries, and the UC8179/UC8279 init tables.
* `src/battery/cw2017_battery.cpp`: read-only CW2017 gauge (I2C 0x63) with a
  last-good / nominal-3.9 V fallback; selected by `BATT_CW2017`.
* `src/power/x4c_stat_power.cpp`: GPIO21 charge STAT (active HIGH) into the
  existing `ChargingStatus` path.
* `include/config.h`: `X4C_SIDE_KEY_0_IS_UP`, the single side-key direction
  define; it still needs one physical press before it is trusted. Also the
  X4C button pin map and `X4C_REFRESH_KEY_MASK`, which selects the manual
  refresh button (see below).
* `docs/xteink-x4c-recovery.md`: ROM-loader recovery, full-flash backup and
  restore, merged/app-only flash offsets, back-to-stock.

## Panel resolution (implemented)

`src/x4c_panel.cpp` runs before `bb_epaper` takes the SPI bus and resolves the
controller with the reference implementations' precedence:

1. factory `hw_calib` / `screenType`: 1 or 0x0B -> UC8179, 2 or 0x0C -> UC8279,
   3 -> SSD1677;
2. the stock `0x70` VER read, clocked back half-duplex on MOSI, id 0x01/0x40 ->
   UC8179, 0x02/0x03/0x41/0x42/0x67/0x68/0x69 -> UC8279;
3. the documented default, UC8279 (every X4C field unit without `hw_calib` has
   reported an UltraChip part; SSD1677 units are expected to carry screenType 3).

The resolved controller selects a `dpList` entry: `EPD_X4C_SSD1677` uses
bb_epaper's proven EP426 SSD1677 profile (the C3 X4's panel family), while
`EPD_X4C_UC8179` and `EPD_X4C_UC8279` share bb_epaper's generic 800x480 UC81xx
base and load the X4C init tables from `src/x4c_panel.cpp` (ported from
Papyrix's `Uc8179X4ProDriver` / `Uc8279X4ProDriver`; the UC8279 table omits the
X4 Pro's PLL command, as the Classic does).

## Manual refresh button (implemented)

The X4 Classic has no OS and no touch panel: every button is a bare GPIO, so a
button action can only be defined in firmware. There is nothing bound at a
device or OS level to reuse.

| Button | GPIO | Behaviour |
|---|---|---|
| Bottom left / right / confirm / back | 5 / 2 / 8 / 9 | **Manual refresh**: wakes the unit, fetches `/api/display`, redraws with a forced full panel update |
| Power | 3 | Wake + the existing press-duration menu (long press opens the settings portal, double click runs the server's special function) |
| Side up / side down | 0 / 7 | Wired and active low; not used by the dashboard |

How the manual refresh works:

1. `src/sleep_session.cpp` arms **ext1** on the RTC GPIO mask in
   `X4C_WAKE_KEY_MASK` (power + the four bottom keys) instead of ext0 on Power
   alone, so a bottom-key press wakes the chip. `src/pins.cpp` gives every wake
   key a pull-up.
2. `src/button.cpp` reads `esp_sleep_get_ext1_wakeup_status()` to learn which key
   woke the unit; that works even for a tap released before boot reaches the
   read. A bottom-key-only wake does not run the Power press-duration menu.
3. `src/wake_init.cpp` sets the `force_full_refresh` flag instead.
4. `src/display_session.cpp` skips the "same filename as last wake, keep the
   screen" short-circuit, so the cached image is re-sent to the panel.
5. `src/display.cpp` forces `REFRESH_FULL` for that wake. On the X4C the UC8279
   path is already a full flash (`x4c_uc8279_full_update()`), so the flag mainly
   documents intent and keeps the behaviour correct if partial updates are added.

The server names each PNG `screen-<sha256>-<changed>`, so the fetch after a
button press is a real `/api/display` round-trip: a changed screen returns a new
filename and downloads normally; an unchanged one keeps the same filename and is
redrawn from flash.

To bind the action to a different key, change `X4C_REFRESH_KEY_MASK` in
`include/config.h`. The remaining keys are `X4C_SIDE_UP_GPIO` and
`X4C_SIDE_DOWN_GPIO`.

## Battery and charger (implemented)

- `BATT_CW2017` / `Cw2017Battery` reads the gauge at I2C 0x63 on SDA 39 / SCL 38
  at 400 kHz. The Classic keeps its factory BATINFO calibration, so the driver
  never writes the 80-byte profile; it validates VERSION (`(v & 0xFD) == 0x0D`)
  and reads VCELL. If the gauge does not answer, the last good value is reused;
  before any reading it reports a nominal 3.9 V with a warning rather than
  failing the boot. `BATT_ADC` is wrong on this board: GPIO0 is a button.
- `X4CStatPower` reads GPIO21 `STAT` (active HIGH) and feeds the existing
  `ChargingStatus` path, so `Battery-Charging` and the status screen reflect it.
  There is no PG/VBUS pin, so USB presence is reported `CONNECTED` only while
  charging and `UNKNOWN` otherwise — no invented DISCONNECTED.

## UC8279 update path — what it takes for an image to appear

Confirmed on the unit on 2026-11-22 with a direct driver using CrossPoint's
reference sequence. The `0x70` probe works, but the generic `bb_epaper` UC81xx
update path cannot drive this glass. Two independent faults:

1. **No plane data.** `bb_epaper`'s `refresh()` sends the init sequence and DRF
   only; pixel data is uploaded exclusively by `writePlane()`. A test that calls
   `refresh()` alone leaves the controller RAM undefined, so the panel stays in
   its undriven striped state even though `refresh()` reports success. The
   shipping path does call `writePlane(PLANE_0)` (DTM2 only) and still misses
   the old-plane seed required by the full-refresh waveform.
2. **Wrong geometry.** The UC8279 is addressed as 800x600 and the visible 480
   gate lines start at gate **120**. A plane upload must be 600 rows of 100
   bytes: 120 white rows, then the 480 image rows. `bb_epaper`'s
   `EP75_800x480` profile streams only 480 rows starting at gate 0, so even a
   correct upload is shifted and the last 120 visible gates stay undefined.

Working full-refresh sequence (verbatim from CrossPoint's `Uc8279X4Driver`):

- Init: PSR `0x00` `0x37 0x4D`; TRES `0x61` 800x600; GSST `0x65` 0/0/0/0; PFS
  `0x03` `0x20`; gate scan `0xE1` `0x02`. No PLL on the X4C; PWR/VDCS/BTST stay
  OTP/MTP-programmed.
- Frame: DTM2 `0x13` <- 600 rows (120 white + image); DTM1 `0x10` <- 600 rows
  white (full-refresh old-plane seed); CDI `0x50` `0x97`; CCSET `0xE0` `0x02`;
  TSSET `0xE5` `0x1E`; PON `0x04`; wait BUSY; PSR `0x00` `0x17 0x4D`; DRF
  `0x12`; wait BUSY; POF `0x02`.
- BUSY is idle-HIGH. A real full refresh drops BUSY after DRF and holds it for
  about 1.24 s.
- Measured on this unit at 10 MHz SPI: upload 102 ms (two 60 KB planes), PON
  40 ms, DRF 1240 ms, POF 77 ms, about 1.46 s per frame, repeatable across
  cycles.

The shipping `env:xteink_x4_classic` build now routes every UC8279 update
through `src/x4c_uc8279.cpp` (called from `display_update_epaper()` and
`display_wipe()`), so the dashboard path uses this sequence. The UC8179 profile
still uses the generic `bb_epaper` path: it needs its own PLL/booster/VCOM
bring-up and has not been validated on hardware.

## Open items that still need the unit

1. **Panel geometry / waveform.** Diagnosed: the generic `bb_epaper` path never
   loads plane data for a standalone refresh and streams 480 rows at gate 0
   instead of 600 rows with the 120-gate offset. See "UC8279 update path" above
   for the verified sequence and timings. Ported for UC8279 in
   `src/x4c_uc8279.cpp`; UC8179 still needs the same treatment plus its own
   power-rail bring-up.
2. **Side-key up/down mapping disagrees between the two sources.** freeink maps
   GPIO0 -> up/previous and GPIO7 -> down/next; Papyrix has `up = 7, down = 0`.
   One `#define` (`X4C_SIDE_KEY_0_IS_UP`) in `include/config.h` makes this a
   one-line change after a physical press. It does not block the panel test.
3. **Power/wake behaviour is unverified on hardware.** Papyrix wakes only on the
   power button (GPIO3); CrossPoint's boot path has a non-interrupt input on
   GPIO4 whose role is still pending. Whether the GPIO1 rail and SD power GPIO6
   must be held in deep sleep, and the resulting sleep current, are measurements.
4. **`LIGHT_SLEEP` vs `DEEP_SLEEP`** for the mono panel is intentionally still
   open: `display_sleep()` uses `LIGHT_SLEEP` for mono panels, and the X4C may
   need `DEEP_SLEEP` so the booster does not drain the battery. Documented here
   until there is a measurement; not changed on a guess.
5. **Flashing discipline.** See `docs/xteink-x4c-recovery.md`: full 16 MB backup
   first, merged image at 0x0, app-only at 0x20000, ROM loader `303A:1001` is the
   recovery path. Stock firmware is `303A:4002` USB-MSC; this build enumerates as
   USB-CDC.
