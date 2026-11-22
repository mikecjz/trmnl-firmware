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

## Open items that block/limit the next milestone

1. **Panel controller auto-detection is not ported.** CrossPoint and Papyrix
   both read command `0x70` back half-duplex on MOSI (no MISO on this board) and
   pick SSD1677 / UC8179 / UC8279; Papyrix additionally falls back to the OEM
   `hw_calib/screenType` byte. `bb_epaper` selects a panel by static panel type
   and has no such read path, so this env uses `EPD_426` (bb_epaper's 4.26"
   800x480 UC81xx profile) as a placeholder. The real per-unit waveform must be
   sorted out before trusting a panel test.
2. **No CW2017 battery driver.** TRMNL supports `BATT_ADC`, `BATT_BQ27220`,
   `BATT_BQ27427`, `BATT_AXP2101`. The X4C uses CW2017 (0x63) on I2C, which is
   not one of them, so this env reports `BATT_NONE` (fake voltage) for now.
   `BATT_ADC` must NOT be used: GPIO0 is a button, not an ADC battery sense.
3. **Side-key up/down mapping disagrees between the two sources.** freeink
   maps GPIO0 -> up/previous and GPIO7 -> down/next; Papyrix's `InputConfig`
   has `up = 7, down = 0`. Confirm on hardware which side is which.
4. **Power/wake behaviour is unverified on hardware.** Papyrix wakes only on
   the power button (GPIO3); CrossPoint's boot path also has a non-interrupt
   input on GPIO4 whose role is still listed as pending. Whether the display
   rail (GPIO1) and SD power (GPIO6) must be held in deep sleep, and the
   resulting sleep current, are measurements, not assumptions.
5. **Flashing discipline.** First flash must be a merged image at 0x0 after a
   full 16 MB `esptool read_flash` backup. The device's ROM bootloader
   (303A:1001) is the recovery path; the stock firmware's USB-MSC identity is
   303A:4002. This TRMNL env builds with `ARDUINO_USB_CDC_ON_BOOT=1`, so the
   dev build enumerates as USB-CDC, not MSC.
