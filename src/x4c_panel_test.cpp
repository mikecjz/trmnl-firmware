// X4C panel-only diagnostic build (-D X4C_PANEL_TEST).
//
// Drives the UC8279 directly with the sequence from CrossPoint's validated
// Uc8279X4Driver (freeink-sdk), because the bb_epaper generic UC81xx path is
// wrong for this glass in two ways:
//
//   1. It never loads the pixel planes unless writePlane() is called; refresh()
//      alone sends init + DRF and leaves the controller RAM undefined.
//   2. The panel is addressed as 800x600 with the visible 480 gates starting at
//      gate 120. Plane uploads must be 120 white rows + 480 image rows (600
//      total, 100 bytes/row); bb_epaper streams only 480 rows from gate 0.
//
// The reference full-refresh trigger order is used verbatim: DTM2 (new image),
// DTM1 (white seed), CDI 0x50/0x97, CCSET 0xE0/0x02, TSSET 0xE5/0x1E, PON,
// wait BUSY (idle = HIGH), PSR rewrite 0x00/0x17/0x4D, DRF 0x12, wait BUSY,
// POF 0x02. No custom LUT: the OTP GC waveform is used.
//
// Boot sequence: BLACK full refresh -> WHITE full refresh -> alignment pattern,
// then repeat, with 8 s / 8 s / 20 s dwell so the result can be photographed at
// any time. Side keys (GPIO0 left, GPIO7 right) are logged continuously.

#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(X4C_PANEL_TEST)

namespace {

constexpr int PIN_SCLK = 12;
constexpr int PIN_MOSI = 11;
constexpr int PIN_CS = 13;
constexpr int PIN_DC = 14;
constexpr int PIN_RST = 10;
constexpr int PIN_BUSY = 18;
constexpr int PIN_PANEL_POWER = 1;
constexpr int PIN_KEY_LEFT = 0;   // side key, up/previous
constexpr int PIN_KEY_RIGHT = 7;  // side key, down/next

constexpr uint32_t SPI_HZ = 10000000;  // reference uses 10 MHz on this panel
constexpr int W = 800;
constexpr int H = 480;
constexpr int STRIDE = W / 8;  // 100 bytes
constexpr int TRES_H = 600;    // addressed gate count
constexpr int GATE_OFFSET = 120;  // first visible gate line

uint8_t g_fb[STRIDE * H];  // 48000 bytes, 1 bpp, bit 1 = white

void out(const char *fmt, ...) {
  char line[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  esp_log_write(ESP_LOG_ERROR, "X4C", "%s\n", line);
}

// ---- bit-banged control over the SPI bus -------------------------------

void csLow() { digitalWrite(PIN_CS, LOW); }
void csHigh() { digitalWrite(PIN_CS, HIGH); }

void cmd(uint8_t c) {
  digitalWrite(PIN_DC, LOW);
  csLow();
  SPI.transfer(c);
  csHigh();
}

void data1(uint8_t d) {
  digitalWrite(PIN_DC, HIGH);
  csLow();
  SPI.transfer(d);
  csHigh();
}

// Idle is HIGH on UC81xx. Returns true when the panel reached idle.
bool waitBusy(uint32_t timeoutMs, uint32_t *waitedMs) {
  const uint32_t t0 = millis();
  delay(5);
  while (digitalRead(PIN_BUSY) == LOW) {
    if (millis() - t0 >= timeoutMs) {
      if (waitedMs) *waitedMs = millis() - t0;
      return false;
    }
    delay(1);
  }
  if (waitedMs) *waitedMs = millis() - t0;
  return true;
}

void resetPanel() {
  gpio_hold_dis(static_cast<gpio_num_t>(PIN_RST));
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, HIGH);
  delay(10);
  digitalWrite(PIN_RST, LOW);
  delay(50);
  digitalWrite(PIN_RST, HIGH);
}

void initPanel() {
  cmd(0x00);  // PSR
  data1(0x37);
  data1(0x4D);
  cmd(0x61);  // TRES 800 x 600 (480 visible)
  data1((W >> 8) & 0xFF);
  data1(W & 0xFF);
  data1((TRES_H >> 8) & 0xFF);
  data1(TRES_H & 0xFF);
  cmd(0x65);  // GSST
  data1(0x00);
  data1(0x00);
  data1(0x00);
  data1(0x00);
  cmd(0x03);  // PFS
  data1(0x20);
  // No PLL on the X4C (stock omits it); PWR/VDCS/BTST stay OTP-programmed.
  cmd(0xE1);  // gate scan
  data1(0x02);
}

// Stream one full 600-gate plane: 120 white rows, then the visible rows, then
// any remaining rows white. CS stays low for the whole upload.
void streamPlane(uint8_t ramCmd, const uint8_t *fb, bool whiteVisibleRows) {
  uint8_t white[STRIDE];
  memset(white, 0xFF, sizeof(white));
  digitalWrite(PIN_DC, LOW);
  csLow();
  SPI.transfer(ramCmd);
  digitalWrite(PIN_DC, HIGH);
  for (int y = 0; y < GATE_OFFSET; y++) SPI.writeBytes(white, STRIDE);
  for (int y = 0; y < H; y++) {
    if (whiteVisibleRows) {
      SPI.writeBytes(white, STRIDE);
    } else {
      SPI.writeBytes(g_fb + y * STRIDE, STRIDE);
    }
  }
  for (int y = GATE_OFFSET + H; y < TRES_H; y++) SPI.writeBytes(white, STRIDE);
  csHigh();
}

void refreshFrame(const char *tag) {
  const uint32_t t0 = millis();
  streamPlane(0x13, g_fb, false);  // DTM2 = new image
  streamPlane(0x10, nullptr, true);  // DTM1 = white seed (clean full flash)
  const uint32_t uploadMs = millis() - t0;

  cmd(0x50); data1(0x97);  // CDI, written on every refresh by stock
  cmd(0xE0); data1(0x02);  // CCSET
  cmd(0xE5); data1(0x1E);  // TSSET, full refresh
  cmd(0x04);               // PON
  uint32_t ponMs = 0;
  const bool ponOk = waitBusy(5000, &ponMs);

  cmd(0x00); data1(0x17); data1(0x4D);  // PSR rewrite between PON and DRF

  digitalWrite(PIN_DC, LOW);
  csLow();
  SPI.transfer(0x12);  // DRF
  csHigh();

  bool dropped = false;
  const uint32_t tDrop = millis();
  while (millis() - tDrop < 100) {
    if (digitalRead(PIN_BUSY) == LOW) {
      dropped = true;
      break;
    }
    delay(1);
  }
  uint32_t drfMs = 0;
  const bool drfOk = waitBusy(20000, &drfMs);

  cmd(0x02);  // POF
  uint32_t pofMs = 0;
  const bool pofOk = waitBusy(5000, &pofMs);

  out("FRAME %s: upload=%ums pon=%ums(%s) drop=%s drf=%ums(%s) pof=%ums(%s) total=%ums", tag,
      (unsigned)uploadMs, (unsigned)ponMs, ponOk ? "ok" : "TIMEOUT", dropped ? "yes" : "NO",
      (unsigned)drfMs, drfOk ? "ok" : "TIMEOUT", (unsigned)pofMs, pofOk ? "ok" : "TIMEOUT",
      (unsigned)(millis() - t0));
}

// ---- drawing ------------------------------------------------------------

inline void px(int x, int y, bool black) {
  if (x < 0 || x >= W || y < 0 || y >= H) return;
  const uint8_t mask = static_cast<uint8_t>(0x80 >> (x & 7));
  if (black) {
    g_fb[y * STRIDE + (x >> 3)] &= static_cast<uint8_t>(~mask);
  } else {
    g_fb[y * STRIDE + (x >> 3)] |= mask;
  }
}

void fillRect(int x, int y, int w, int h, bool black) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) px(xx, yy, black);
}

void line(int x0, int y0, int x1, int y1) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    px(x0, y0, true);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void drawPattern() {
  memset(g_fb, 0xFF, sizeof(g_fb));  // white
  fillRect(0, 0, W, 4, true);        // frame, all four edges
  fillRect(0, H - 4, W, 4, true);
  fillRect(0, 0, 4, H, true);
  fillRect(W - 4, 0, 4, H, true);

  fillRect(4, 4, 16, 16, true);  // corner squares
  fillRect(W - 20, 4, 16, 16, true);
  fillRect(4, H - 20, 16, 16, true);
  fillRect(W - 20, H - 20, 16, 16, true);

  // Asymmetric orientation markers: wide bar top-left, tall bar bottom-right.
  fillRect(40, 40, 120, 24, true);
  fillRect(W - 64, H - 160, 24, 120, true);

  line(W / 2, 0, W / 2, H - 1);  // centre cross
  line(0, H / 2, W - 1, H / 2);
  line(4, 4, W - 5, H - 5);  // diagonals
  line(W - 5, 4, 4, H - 5);

  for (int x = 0; x < W; x += 50) line(x, 4, x, (x % 100 == 0) ? 24 : 14);
  for (int y = 0; y < H; y += 50) line(4, y, (y % 100 == 0) ? 24 : 14, y);

  for (int x = 0; x < W; x += 80) fillRect(x, 30, 40, 10, true);  // top band
  for (int x = 0; x < W; x += 2) line(x, H - 16, x, H - 10);      // bottom stripes
}

// ---- keys / dwell -------------------------------------------------------

void dwell(uint32_t ms) {
  static bool left = false, right = false;
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    const bool nl = digitalRead(PIN_KEY_LEFT) == LOW;
    const bool nr = digitalRead(PIN_KEY_RIGHT) == LOW;
    if (nl != left) out("KEY LEFT (GPIO0) %s", nl ? "pressed" : "released");
    if (nr != right) out("KEY RIGHT (GPIO7) %s", nr ? "pressed" : "released");
    left = nl;
    right = nr;
    delay(10);
  }
}

}  // namespace

void x4c_panel_test_run() {
  delay(300);
  out("=== X4C UC8279 DIRECT DIAGNOSTIC (reference sequence) ===");
  out("chip=%s rev=%u reset_reason=%d", ESP.getChipModel(), ESP.getChipRevision(),
      static_cast<int>(esp_reset_reason()));
  out("pins: sclk=%d mosi=%d cs=%d dc=%d rst=%d busy=%d power=%d", PIN_SCLK, PIN_MOSI, PIN_CS,
      PIN_DC, PIN_RST, PIN_BUSY, PIN_PANEL_POWER);

  pinMode(PIN_PANEL_POWER, OUTPUT);
  digitalWrite(PIN_PANEL_POWER, HIGH);
  delay(150);
  out("rail: gpio%d=%d busy=%d (before reset)", PIN_PANEL_POWER,
      digitalRead(PIN_PANEL_POWER), digitalRead(PIN_BUSY));

  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  pinMode(PIN_DC, OUTPUT);
  digitalWrite(PIN_DC, HIGH);
  pinMode(PIN_MOSI, OUTPUT);
  pinMode(PIN_SCLK, OUTPUT);
  pinMode(PIN_BUSY, INPUT);
  pinMode(PIN_KEY_LEFT, INPUT_PULLUP);
  pinMode(PIN_KEY_RIGHT, INPUT_PULLUP);

  SPI.begin(PIN_SCLK, -1, PIN_MOSI, -1);
  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));

  resetPanel();
  uint32_t busMs = 0;
  const bool busyOk = waitBusy(1000, &busMs);
  out("after reset: busy=%d idle=%s wait=%ums", digitalRead(PIN_BUSY), busyOk ? "ok" : "TIMEOUT",
      (unsigned)busMs);

  initPanel();
  out("init sent (PSR 37/4D, TRES 800x600, GSST 0, PFS 20, E1 02)");

  uint32_t cycle = 0;
  for (;;) {
    cycle++;
    memset(g_fb, 0x00, sizeof(g_fb));
    refreshFrame("BLACK");
    dwell(8000);

    memset(g_fb, 0xFF, sizeof(g_fb));
    refreshFrame("WHITE");
    dwell(8000);

    drawPattern();
    refreshFrame("PATTERN");
    dwell(20000);
    out("cycle %u complete", (unsigned)cycle);
  }
}

#endif  // X4C_PANEL_TEST
