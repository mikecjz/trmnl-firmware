// X4C UC8279 full-refresh transport — see include/x4c_uc8279.h.
//
// Reference: CrossPoint freeink-sdk Uc8279X4Driver (hardware-validated on an
// X4C UC8279 unit). Sequence and timing copied, not guessed:
//
//   init (sent once by bb_epaper initIO via x4c_apply_panel_overrides):
//     PSR 0x00/0x37/0x4D, TRES 0x61/800/600, GSST 0x65/0/0/0/0,
//     PFS 0x03/0x20, gate scan 0xE1/0x02. No PLL on the X4C; PWR/VDCS/BTST stay
//     OTP/MTP-programmed.
//
//   every full update:
//     DTM2 0x13 <- 600 rows (120 white + visible rows + pad white)
//     DTM1 0x10 <- 600 rows white (full-refresh old-plane seed)
//     CDI 0x50/0x97, CCSET 0xE0/0x02, TSSET 0xE5/0x1E
//     PON 0x04, wait BUSY
//     PSR 0x00/0x17/0x4D (REG cleared -> OTP waveform)
//     DRF 0x12, wait BUSY, POF 0x02, wait BUSY
//
// BUSY is idle-HIGH; a full refresh holds it low for about 1.24 s.

#include "x4c_uc8279.h"

#if defined(BOARD_XTEINK_X4_CLASSIC) && !defined(PARALLEL_EPD)

#include <Arduino.h>
#include <SPI.h>
#include <board_config.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "x4c_panel.h"

namespace {

constexpr int kTresHeight = 600;   // addressed gate count
constexpr int kGateOffset = 120;   // first visible gate line
constexpr int kMaxStride = 128;    // 1024 px at 1 bpp
constexpr uint32_t kSpiHz = 10000000;
constexpr uint32_t kPonTimeoutMs = 5000;
constexpr uint32_t kDrfTimeoutMs = 20000;
constexpr uint32_t kPofTimeoutMs = 5000;

int s_sck = -1, s_mosi = -1, s_cs = -1, s_dc = -1, s_rst = -1, s_busy = -1;

void out(const char *fmt, ...) {
  char line[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  // ESP_LOG is visible on the boot console (HWCDC drops plain Serial output
  // until a host drains it).
  esp_log_write(ESP_LOG_WARN, "X4C", "%s\n", line);
}

void csLow() { digitalWrite(s_cs, LOW); }
void csHigh() { digitalWrite(s_cs, HIGH); }

void cmd(uint8_t c) {
  digitalWrite(s_dc, LOW);
  csLow();
  SPI.transfer(c);
  csHigh();
}

void data1(uint8_t d) {
  digitalWrite(s_dc, HIGH);
  csLow();
  SPI.transfer(d);
  csHigh();
}

bool waitIdle(uint32_t timeoutMs, uint32_t *waitedMs) {
  const uint32_t t0 = millis();
  delay(5);  // BUSY is not valid immediately after the command
  while (digitalRead(s_busy) == LOW) {
    if (millis() - t0 >= timeoutMs) {
      if (waitedMs) *waitedMs = millis() - t0;
      return false;
    }
    delay(1);
  }
  if (waitedMs) *waitedMs = millis() - t0;
  return true;
}

// One full 600-gate plane: 120 white rows, then either the visible rows or
// white, then pad white. CS stays asserted for the whole upload.
void streamPlane(uint8_t ramCmd, const uint8_t *fb, int stride, int height, bool whiteRows) {
  uint8_t white[kMaxStride];
  memset(white, 0xFF, sizeof(white));
  digitalWrite(s_dc, LOW);
  csLow();
  SPI.transfer(ramCmd);
  digitalWrite(s_dc, HIGH);
  for (int y = 0; y < kGateOffset; y++) SPI.writeBytes(white, stride);
  for (int y = 0; y < height; y++) {
    if (whiteRows) {
      SPI.writeBytes(white, stride);
    } else {
      SPI.writeBytes(fb + static_cast<size_t>(y) * stride, stride);
    }
  }
  for (int y = kGateOffset + height; y < kTresHeight; y++) SPI.writeBytes(white, stride);
  csHigh();
}

}  // namespace

bool x4c_uc8279_active() {
  return pDevice != nullptr && x4c_active_panel() == X4cPanel::UC8279;
}

bool x4c_uc8279_full_update(const uint8_t *fb, uint16_t width, uint16_t height) {
  if (!x4c_uc8279_active() || fb == nullptr) return false;
  if (width != 800 || height == 0 || height > 480) {
    out("UC8279: unsupported geometry %ux%u", (unsigned)width, (unsigned)height);
    return false;
  }
  const int stride = width / 8;

#ifdef X4C_MARKER_FRAME
  // Marker frame for measuring the source-row -> glass mapping. Uploaded through
  // the unchanged transport. Layout (480 rows, 800 px):
  //   - full-width 1 px black row at y = 0, 40, 80 ... 440 (12 bands)
  //   - inside each band, rows y+3..y+6 are black from x=0 to x=40*(k+1),
  //     so the bar length encodes the band index k (40 px .. 480 px)
  //   - 4 px border, plus a 40x40 solid block at the bottom-right only
  static uint8_t marker[100 * 480];
  static bool marker_ready = false;
  if (!marker_ready) {
    memset(marker, 0xFF, sizeof(marker));
    auto hline = [&](int y, int x0, int x1) {
      for (int x = x0; x <= x1 && x < 800; x++) marker[y * 100 + (x >> 3)] &= (uint8_t)~(0x80 >> (x & 7));
    };
    for (int y = 0; y < 480; y += 40) {
      hline(y, 0, 799);
      const int k = y / 40;
      for (int yy = y + 3; yy <= y + 6 && yy < 480; yy++) hline(yy, 0, 40 * (k + 1) - 1);
    }
    for (int y = 0; y < 4; y++) { hline(y, 0, 799); hline(479 - y, 0, 799); }
    for (int y = 0; y < 480; y++) { hline(y, 0, 3); hline(y, 796, 799); }
    for (int y = 436; y < 476; y++) hline(y, 756, 795);
    marker_ready = true;
    out("MARKER frame active: bands every 40 rows, bar length = 40*(k+1)");
  }
  fb = marker;
#endif

  s_sck = pDevice->epd_sck_pin;
  s_mosi = pDevice->epd_mosi_pin;
  s_cs = pDevice->epd_cs_pin;
  s_dc = pDevice->epd_dc_pin;
  s_rst = pDevice->epd_rst_pin;
  s_busy = pDevice->epd_busy_pin;
  if (s_sck < 0 || s_mosi < 0 || s_cs < 0 || s_dc < 0 || s_busy < 0) return false;

  pinMode(s_cs, OUTPUT);
  digitalWrite(s_cs, HIGH);
  pinMode(s_dc, OUTPUT);
  digitalWrite(s_dc, HIGH);
  pinMode(s_busy, INPUT);

#ifdef X4C_FB_DUMP
  // One-shot compact fingerprint of the framebuffer: CRC32 per row. Small enough
  // not to trip the task watchdog, and enough to see whether the source image is
  // duplicated (repeated row-hash sequences) before it reaches the panel.
  static int dumps_left = 10;
  if (dumps_left > 0) {
    dumps_left--;
    out("FBROWS begin %ux%u stride=%d total=%u", (unsigned)width, (unsigned)height, stride,
        (unsigned)(stride * height));
    for (int y = 0; y < height; y++) {
      uint32_t crc = 0xffffffffu;
      const uint8_t *row = fb + static_cast<size_t>(y) * stride;
      for (int i = 0; i < stride; i++) {
        crc ^= row[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
      }
      esp_log_write(ESP_LOG_ERROR, "X4C", "FBROW %03d %08x\n", y, (unsigned)(~crc));
      if ((y & 31) == 31) esp_task_wdt_reset();
    }
    out("FBROWS end");
  }
#endif

  const uint32_t t0 = millis();
  SPI.beginTransaction(SPISettings(kSpiHz, MSBFIRST, SPI_MODE0));

  streamPlane(0x13, fb, stride, height, false);  // DTM2 = new image
  streamPlane(0x10, nullptr, stride, height, true);  // DTM1 = white seed
  const uint32_t uploadMs = millis() - t0;

  cmd(0x50);
  data1(0x97);  // CDI, written on every refresh by stock
  cmd(0xE0);
  data1(0x02);  // CCSET
  cmd(0xE5);
  data1(0x1E);  // TSSET, full refresh
  cmd(0x04);    // PON
  uint32_t ponMs = 0;
  const bool ponOk = waitIdle(kPonTimeoutMs, &ponMs);

  cmd(0x00);
  data1(0x17);
  data1(0x4D);  // PSR rewrite between PON and DRF
  cmd(0x12);    // DRF

  bool dropped = false;
  const uint32_t tDrop = millis();
  while (millis() - tDrop < 100) {
    if (digitalRead(s_busy) == LOW) {
      dropped = true;
      break;
    }
    delay(1);
  }
  uint32_t drfMs = 0;
  const bool drfOk = waitIdle(kDrfTimeoutMs, &drfMs);

  cmd(0x02);  // POF
  uint32_t pofMs = 0;
  const bool pofOk = waitIdle(kPofTimeoutMs, &pofMs);

  SPI.endTransaction();

  out("UC8279 frame: upload=%ums pon=%ums(%s) drop=%s drf=%ums(%s) pof=%ums(%s) total=%ums",
      (unsigned)uploadMs, (unsigned)ponMs, ponOk ? "ok" : "TIMEOUT", dropped ? "yes" : "NO",
      (unsigned)drfMs, drfOk ? "ok" : "TIMEOUT", (unsigned)pofMs, pofOk ? "ok" : "TIMEOUT",
      (unsigned)(millis() - t0));
  return ponOk && drfOk && pofOk;
}

#endif  // BOARD_XTEINK_X4_CLASSIC && !PARALLEL_EPD
