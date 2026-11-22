#pragma once

#include <stdint.h>

#include <config.h>

class BBEPAPER;

// Panel controllers that Xteink shipped in the X4 Classic (X4 v2), in the
// order the boot probe resolves them.
enum class X4cPanel : uint8_t {
  SSD1677 = 0,
  UC8179,
  UC8279,
};

// Resolve the panel controller for the running unit. Precedence, matching the
// CrossPoint/Papyrix reference implementations:
//   1. factory `hw_calib`/`screenType` in NVS (1/0x0B = UC8179, 2/0x0C = UC8279,
//      3 = SSD1677);
//   2. the stock 0x70 VER read, clocked back half-duplex on MOSI (the board has
//      no MISO);
//   3. the documented default, UC8279, which is what every X4C field unit
//      without an `hw_calib` entry has reported so far.
// Idempotent; also selects the active dpList entry. Call before display_init()
// takes over the SPI bus.
X4cPanel x4c_detect_panel(TRMNL_DEVICE *device);

// Panel set index in dpList for a controller.
uint8_t x4c_panel_set(X4cPanel panel);
const char *x4c_panel_name(X4cPanel panel);

// The controller chosen by x4c_detect_panel(); SSD1677 until it has run.
X4cPanel x4c_active_panel();

// Replaces the generic bb_epaper UC81xx init with the X4C sequence after
// setPanelType(). No-op for SSD1677, whose EP426 profile already matches the
// C3 X4 (same 800x480 glass family).
void x4c_apply_panel_overrides(BBEPAPER &bbep, X4cPanel panel);
