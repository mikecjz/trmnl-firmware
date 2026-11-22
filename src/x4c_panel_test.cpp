// X4C panel-only test build (-D X4C_PANEL_TEST).
//
// Initialises ONLY the display through the shipping driver path (board config,
// 0x70 controller probe, X4C UC81xx init table, bb_epaper), draws an 800x480
// alignment pattern and does one full refresh. No filesystem, network, server,
// sleep or OTA code runs, and nothing is written to the panel outside the
// normal full-refresh image.
//
// The pattern is designed to be verified by eye:
//   * 4 px black frame on all four edges -> cropping or a shifted origin is
//     immediately visible as a missing/broken border
//   * filled 16 px corner squares and a centre cross
//   * 50 px ruler ticks along the top and left edges
//   * diagonals corner-to-corner -> square pixels and no line-shear
//   * text markers: left/right key labels and the controller name
//
// Keys are polled after the refresh and logged so a single press can be
// attributed to GPIO0 (left/previous) or GPIO7 (right/next).

#include <Arduino.h>
#include <bb_epaper.h>
#include <epd_driver.h>
#include <esp_log.h>
#include <stdarg.h>
#include <stdio.h>

#include "display.h"
#include "x4c_panel.h"

#if defined(X4C_PANEL_TEST)

namespace {

constexpr int PIN_PANEL_POWER = 1;
constexpr int PIN_KEY_LEFT = 0;   // side key, up/previous
constexpr int PIN_KEY_RIGHT = 7;  // side key, down/next

void out(const char *fmt, ...) {
  char line[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  esp_log_write(ESP_LOG_ERROR, "X4C", "%s\n", line);
}

}  // namespace

void x4c_panel_test_run() {
  delay(200);

  // Master peripheral/panel rail, active HIGH (CrossPoint X4C profile).
  pinMode(PIN_PANEL_POWER, OUTPUT);
  digitalWrite(PIN_PANEL_POWER, HIGH);
  delay(100);

  out("=== X4C PANEL-ONLY TEST: display driver path only ===");
  out("build: 800x480, controller resolved by 0x70 probe");

  display_init();
  const uint16_t w = display_width();
  const uint16_t h = display_height();
  out("display_init ok: %ux%u controller=%s", (unsigned)w, (unsigned)h,
      x4c_panel_name(x4c_active_panel()));

  bbep.setMemoryMode(BBEP_MODE_1BPP);
  const int alloc = bbep.allocBuffer(false);
  out("allocBuffer rc=%d width=%u height=%u", alloc, (unsigned)bbep.width(),
      (unsigned)bbep.height());

  bbep.fillScreen(BBEP_WHITE);

  // Frame: every edge, 4 px.
  bbep.fillRect(0, 0, w, 4, BBEP_BLACK);
  bbep.fillRect(0, h - 4, w, 4, BBEP_BLACK);
  bbep.fillRect(0, 0, 4, h, BBEP_BLACK);
  bbep.fillRect(w - 4, 0, 4, h, BBEP_BLACK);

  // Corner squares, 16 px, inset by the frame.
  bbep.fillRect(4, 4, 16, 16, BBEP_BLACK);
  bbep.fillRect(w - 20, 4, 16, 16, BBEP_BLACK);
  bbep.fillRect(4, h - 20, 16, 16, BBEP_BLACK);
  bbep.fillRect(w - 20, h - 20, 16, 16, BBEP_BLACK);

  // Centre cross.
  bbep.drawLine(w / 2, 0, w / 2, h - 1, BBEP_BLACK);
  bbep.drawLine(0, h / 2, w - 1, h / 2, BBEP_BLACK);

  // Diagonals: shear or aspect errors show as kinks against the corners.
  bbep.drawLine(4, 4, w - 5, h - 5, BBEP_BLACK);
  bbep.drawLine(w - 5, 4, 4, h - 5, BBEP_BLACK);

  // Rulers every 50 px, longer ticks every 100 px.
  for (int x = 0; x < w; x += 50) {
    bbep.drawLine(x, 4, x, (x % 100 == 0) ? 24 : 14, BBEP_BLACK);
  }
  for (int y = 0; y < h; y += 50) {
    bbep.drawLine(4, y, (y % 100 == 0) ? 24 : 14, y, BBEP_BLACK);
  }

  // Top band: alternating 40 px blocks, shows banding/dropped columns.
  for (int x = 0; x < w; x += 80) {
    bbep.fillRect(x, 30, 40, 10, BBEP_BLACK);
  }
  // Bottom band: 1 px on / 1 px off, shows pixel-level artefacts.
  for (int x = 0; x < w; x += 2) {
    bbep.drawLine(x, h - 16, x, h - 10, BBEP_BLACK);
  }

  bbep.setFont(FONT_16x16);
  bbep.setTextColor(BBEP_BLACK, BBEP_WHITE);
  bbep.drawString("X4C PANEL TEST", 300, 170);
  bbep.setFont(FONT_12x16);
  bbep.drawString("UC8279", 360, 200);
  bbep.drawString("800 x 480", 345, 222);

  bbep.setFont(FONT_8x8);
  bbep.drawString("LEFT KEY / GPIO0", 24, 300);
  bbep.drawString("RIGHT KEY / GPIO7", 620, 300);

  out("pattern drawn, full refresh...");
  bbep.refresh(REFRESH_FULL, true);
  out("full refresh returned");

  // One-shot dump of the 1-bpp framebuffer that was pushed to the panel, so the
  // pattern can be reconstructed and compared against what the panel shows.
  {
    const uint8_t *fb = static_cast<const uint8_t *>(bbep.getBuffer());
    const size_t stride = (w + 7) / 8;
    const size_t total = stride * h;
    if (fb != nullptr) {
      out("FB begin %ux%u stride=%u total=%u", (unsigned)w, (unsigned)h,
          (unsigned)stride, (unsigned)total);
      for (size_t off = 0; off < total; off += 32) {
        char line[160];
        int n = snprintf(line, sizeof(line), "FB %06x ", (unsigned)off);
        for (size_t i = 0; i < 32 && off + i < total; i++) {
          n += snprintf(line + n, sizeof(line) - n, "%02x", fb[off + i]);
        }
        esp_log_write(ESP_LOG_ERROR, "X4C", "%s\n", line);
      }
      out("FB end");
    } else {
      out("FB missing");
    }
  }

  pinMode(PIN_KEY_LEFT, INPUT_PULLUP);
  pinMode(PIN_KEY_RIGHT, INPUT_PULLUP);

  bool left = false, right = false;
  uint32_t lastAlive = millis();
  for (;;) {
    const bool nl = digitalRead(PIN_KEY_LEFT) == LOW;
    const bool nr = digitalRead(PIN_KEY_RIGHT) == LOW;
    if (nl != left) out("KEY LEFT (GPIO0) %s", nl ? "pressed" : "released");
    if (nr != right) out("KEY RIGHT (GPIO7) %s", nr ? "pressed" : "released");
    left = nl;
    right = nr;
    if (millis() - lastAlive >= 5000) {
      lastAlive = millis();
      out("ALIVE controller=%s %ux%u", x4c_panel_name(x4c_active_panel()), (unsigned)w,
          (unsigned)h);
    }
    delay(20);
  }
}

#endif  // X4C_PANEL_TEST
