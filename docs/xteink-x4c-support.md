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
   The pull-up must be the **RTC pad pull-up** (`rtc_gpio_pullup_en`), not just
   `pinMode(INPUT_PULLUP)`: the digital pull-up is lost with the digital domain
   in deep sleep, and these pins have no external pull-up. Without it the pins
   float low, ext1 fires the instant the chip sleeps and the unit boot-loops
   (wake logo, dashboard, sleep, immediate wake). Any key that still reads low at
   sleep time — held, stuck, or pressed by a stand — is left out of that sleep's
   ext1 mask for the same reason; if none are released the wake is timer-only.
   The five levels and the armed mask are logged as
   `X4C: sleep: keys p3=… p5=… p2=… p8=… p9=… mask=… draws=…`, where `draws` is
   the number of panel updates in that boot (partial and full together).
2. `src/button.cpp` reads `esp_sleep_get_ext1_wakeup_status()` to learn which key
   woke the unit; that works even for a tap released before boot reaches the
   read. A bottom-key-only wake does not run the Power press-duration menu. A
   `X4C: wake: reason=… mask=… prev_reason=… prev_mask=… prev_draws=…` line
   records the previous natural wake (and its draw count) in RTC memory, because
   opening the USB console resets the chip and loses the live wake cause. On a
   refresh-key wake `prev_draws=1` means the logo frame was skipped.
3. `src/wake_init.cpp` sets the `force_full_refresh` flag instead.
4. Because that flag is set before the boot's first draw, `src/bl.cpp` also skips
   `showWakeLogo()` on a refresh-key wake: the press goes straight to the fetch
   with the previous dashboard left on the panel, then one full refresh. Power
   and cold boots still show the logo.
5. `src/display_session.cpp` skips the "same filename as last wake, keep the
   screen" short-circuit, so the cached image is re-sent to the panel.
6. `src/display.cpp` forces `REFRESH_FULL` for that wake. The UC8279 path treats
   that as an unconditional OTP flash (`x4c_uc8279_full_update()`), bypassing the
   dirty-rect diff and the DU partial, and restarts the partial/ghosting counter
   (`iUpdateCount = 0`). This is the one button path that is deliberately a
   flash: bottom left / right / confirm / back (GPIO 5 / 2 / 8 / 9).
7. Presses while the unit is **awake** are caught too. ext1 only sees a key while
   the chip sleeps, and at a short refresh interval most of a cycle is awake time
   (boot, Wi-Fi, fetch, draw; the fetch grid takes that time out of the sleep).
   `src/x4c_refresh_keys.cpp` samples the four bottom keys every 10 ms, started
   first thing in `bl_init()` (debounced by `lib/trmnl` `key_press_latch`). A
   key that is already down then and comes up within 1 s was tapped during the
   boot window and counts too; the key that woke the chip, a deferred press's
   key, or a key held longer (stuck, held by a stand) does not. Where the press
   lands decides what serves it:
   - before the fetch starts: that fetch becomes the manual refresh;
   - during the fetch (after the download, before the draw): that draw becomes
     the full flash, since the image was just fetched (no second fetch);
   - during the draw: Wi-Fi is kept up through the draw on this board, so
     `bl.cpp` runs the manual fetch straight away on the open connection (at
     most 2 per wake);
   - as the unit goes to sleep: `arm_x4c_wake_keys()` records it in RTC memory
     and arms a 100 ms timer, so the next wake does the manual refresh.
   A press stays owed until a manual fetch for it succeeds. If it fails (Wi-Fi,
   server, an error path that sleeps), sleep entry retries it once on a 100 ms
   re-wake; a second failure is dropped so a dead server cannot loop.
   A deferred or key wake rejoins the last AP on its channel without a scan and
   reuses that join's DHCP lease (`src/wifi_network.cpp`, from PR #1 plus the
   lease). A failed manual fetch on a reused lease forgets it.
   The sampling task itself never logs: `esp_log_write()` needs more stack than
   the task has (newlib's vfprintf alone is an 800-byte frame), and an early
   build that logged from the task panic-reset on every awake press.

## Refresh-key tap trace (diagnostics)

Taps are invisible over USB while they happen: opening the console resets the
chip, and the USB link drops in deep sleep. So the firmware records them itself
(`src/x4c_tap_trace.cpp`, format and ring in `lib/trmnl` `tap_trace`), in a
64-record ring in RTC no-init memory that survives deep sleep and the console's
chip reset. It adds no network traffic and no flash writes, so it does not
change the timings it measures.

**Reading it:** after tapping, plug in USB and open the console (115200). The
boot that follows is not a deep-sleep wake, so it prints the whole ring,
oldest first, between `X4C: tap trace: N records` and `X4C: tap trace: end`.

Times are RTC milliseconds (`esp_rtc_get_time_us() / 1000`), which keep
counting through deep sleep. "+N" values are ms since the wake's app start
(ROM + bootloader, about 0.35 s, are not included).

| Line | Fields |
|---|---|
| `cycle` (one per wake, at sleep entry) | `wake@` RTC ms of the wake; `reason` (3 = ext1 key, 4 = timer, 0 = reset); `ext1` wake mask; `wifi` / `fetch` / `dl` / `draw` / `sleep` phase times (`-` = not reached); `armed` sleep; `epoch` wall clock at sleep entry (convert any RTC time with `epoch - (sleep_rtc - t) / 1000`); `low_samples` key samples that read low; `task_presses` debounced presses; flags `deferred-in`, `deferred-out`, `manual`, `flashed`, `fast-rejoin`, `lease` |
| `press` | `@` RTC ms of the press; `src` = `task` (awake sampler), `boot-grace` (tapped before sampling started), `sleep-snapshot` (last check before sleep), `key-wake` (ext1, time = wake), `deferred-wake` (the 100 ms re-wake; time = the original press), `retry` (a failed manual fetch tried again); `phase` = `before-fetch`, `before-draw`, `after-fetch`, `sleep-entry`, `wake`; `keys` GPIO mask |
| `manual` | what the press led to: `press->fetch` and `press->flash` delays (`never` = no flash for it), `result` (3 = success), `second-fetch` when it needed a fetch of its own |

A tap that has no `press` line at its wall-clock time was missed. The `cycle`
line around that time tells whether the device was asleep, booting before the
key watcher started, or awake (`low_samples` > 0 with no press = shorter than
the 20 ms debounce).
