// Xteink X4 Classic (X4 v2) panel resolution.
//
// The board has no MISO line: the controller's VER register (command 0x70) is
// clocked back half-duplex on MOSI. The bit-bang below follows the MIT-licensed
// Papyrix X4C probe (lib/EInkDisplay/src/X4ClassicDisplayProbe.cpp) and
// CrossPoint's freeink-sdk XteinkDetect, which were both validated against stock
// X4C firmware. Pin/timeout values are not guesses.
//
// This runs before bb_epaper owns the SPI peripheral and leaves every display
// pin as an input, so the normal display init can reconfigure them.

#include <Arduino.h>
#include <ArduinoLog.h>
#include <Preferences.h>
#include <board_config.h>
#include <epd_driver.h>
#include <x4c_panel.h>

#if !defined(PARALLEL_EPD)

namespace {

constexpr uint8_t kCmdReadVer = 0x70;
constexpr uint16_t kBusyTimeoutMs = 300;
constexpr uint8_t kPinUnused = 0xff;

struct ProbePins {
  uint8_t sclk, mosi, cs, dc, rst, busy;
};

bool pinsUsable(const ProbePins &p) {
  return p.sclk != kPinUnused && p.mosi != kPinUnused && p.cs != kPinUnused && p.dc != kPinUnused &&
         p.rst != kPinUnused && p.busy != kPinUnused;
}

void release(ProbePins p) {
  pinMode(p.sclk, INPUT);
  pinMode(p.mosi, INPUT);
  pinMode(p.cs, INPUT_PULLUP);
  pinMode(p.dc, INPUT);
  pinMode(p.rst, INPUT);
  pinMode(p.busy, INPUT);
}

// Mirrors Papyrix: reset the panel, wait for BUSY to go high, issue 0x70 and
// clock three bytes in on MOSI. Returns false on a timeout or BUSY ambiguity.
bool readVerBytes(ProbePins p, uint8_t ver[3]) {
  pinMode(p.cs, OUTPUT);
  digitalWrite(p.cs, HIGH);
  pinMode(p.sclk, OUTPUT);
  digitalWrite(p.sclk, LOW);
  pinMode(p.dc, OUTPUT);
  digitalWrite(p.dc, LOW);
  pinMode(p.mosi, OUTPUT);
  pinMode(p.busy, INPUT);

  gpio_hold_dis(static_cast<gpio_num_t>(p.rst));
  pinMode(p.rst, OUTPUT);
  digitalWrite(p.rst, HIGH);
  delay(10);
  digitalWrite(p.rst, LOW);
  delay(50);
  digitalWrite(p.rst, HIGH);

  const uint32_t start = millis();
  while (digitalRead(p.busy) == LOW) {
    if (millis() - start >= kBusyTimeoutMs) {
      release(p);
      return false;
    }
    delay(1);
  }

  digitalWrite(p.cs, LOW);
  uint8_t command = kCmdReadVer;
  for (unsigned bit = 0; bit < 8; ++bit) {
    digitalWrite(p.mosi, (command & 0x80) ? HIGH : LOW);
    delayMicroseconds(1);
    digitalWrite(p.sclk, HIGH);
    delayMicroseconds(1);
    digitalWrite(p.sclk, LOW);
    command <<= 1;
  }

  digitalWrite(p.dc, HIGH);
  pinMode(p.mosi, INPUT);
  for (unsigned idx = 0; idx < 3; ++idx) {
    uint8_t byte = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
      delayMicroseconds(1);
      digitalWrite(p.sclk, HIGH);
      delayMicroseconds(1);
      byte = static_cast<uint8_t>((byte << 1) | (digitalRead(p.mosi) == HIGH));
      digitalWrite(p.sclk, LOW);
    }
    ver[idx] = byte;
  }
  digitalWrite(p.cs, HIGH);
  release(p);
  return true;
}

// Factory screenType written by stock into hw_calib. Returns false when the key
// is absent; an unrecognised value still counts as present so the caller can
// fall through to the probe.
bool readFactoryScreenType(uint8_t *value) {
  Preferences prefs;
  if (!prefs.begin("hw_calib", true)) return false;
  const bool present = prefs.isKey("screenType");
  if (present) *value = prefs.getUChar("screenType", 0);
  prefs.end();
  return present;
}

bool panelFromFactoryValue(uint8_t value, X4cPanel *panel) {
  switch (value) {
    case 1:
    case 0x0B:
      *panel = X4cPanel::UC8179;
      return true;
    case 2:
    case 0x0C:
      *panel = X4cPanel::UC8279;
      return true;
    case 3:
      *panel = X4cPanel::SSD1677;
      return true;
    default:
      return false;
  }
}

bool panelFromProbeId(uint8_t id, X4cPanel *panel) {
  switch (id) {
    case 0x01:
    case 0x40: // QY/BOE 4.28, UC8179
      *panel = X4cPanel::UC8179;
      return true;
    case 0x02:
    case 0x03:
    case 0x41:
    case 0x42:
    case 0x67:
    case 0x68:
    case 0x69: // QY/BOE/ZHX 4.28/4.42, UC8279
      *panel = X4cPanel::UC8279;
      return true;
    default:
      return false;
  }
}

} // namespace

static X4cPanel g_active_panel = X4cPanel::SSD1677;
static bool g_detected = false;

const char *x4c_panel_name(X4cPanel panel) {
  switch (panel) {
    case X4cPanel::SSD1677:
      return "SSD1677";
    case X4cPanel::UC8179:
      return "UC8179";
    case X4cPanel::UC8279:
      return "UC8279";
  }
  return "unknown";
}

uint8_t x4c_panel_set(X4cPanel panel) {
  switch (panel) {
    case X4cPanel::SSD1677:
      return EPD_X4C_SSD1677;
    case X4cPanel::UC8179:
      return EPD_X4C_UC8179;
    case X4cPanel::UC8279:
      return EPD_X4C_UC8279;
  }
  return EPD_X4C_SSD1677;
}

X4cPanel x4c_active_panel() { return g_active_panel; }

X4cPanel x4c_detect_panel(TRMNL_DEVICE *device) {
  if (g_detected) return g_active_panel;
  g_detected = true;
  g_active_panel = X4cPanel::UC8279; // documented fallback
  if (device == nullptr) {
    set_active_panel_set(x4c_panel_set(g_active_panel));
    return g_active_panel;
  }

  uint8_t factory = 0;
  const bool haveFactory = readFactoryScreenType(&factory);
  if (haveFactory && panelFromFactoryValue(factory, &g_active_panel)) {
    Log.info("X4C panel: %s (factory screenType=%u)", x4c_panel_name(g_active_panel), factory);
    set_active_panel_set(x4c_panel_set(g_active_panel));
    return g_active_panel;
  }

  const ProbePins pins{device->epd_sck_pin, device->epd_mosi_pin, device->epd_cs_pin, device->epd_dc_pin,
                       device->epd_rst_pin, device->epd_busy_pin};
  uint8_t ver[3] = {0};
  if (pinsUsable(pins) && readVerBytes(pins, ver)) {
    Log.info("X4C panel: VER=%02X %02X %02X", ver[0], ver[1], ver[2]);
    if (panelFromProbeId(ver[2], &g_active_panel)) {
      Log.info("X4C panel: %s (0x70 probe id=%02X)", x4c_panel_name(g_active_panel), ver[2]);
      set_active_panel_set(x4c_panel_set(g_active_panel));
      return g_active_panel;
    }
  }

  // Unrecognised silicon: keep the reference implementations' behaviour and use
  // the UC8279 profile. SSD1677 units are expected to carry screenType=3.
  Log.warning("X4C panel: probe inconclusive%s; defaulting to UC8279",
              haveFactory ? " (factory screenType unrecognised)" : " (no factory screenType)");
  set_active_panel_set(x4c_panel_set(g_active_panel));
  return g_active_panel;
}

// UC8179 full init, from Papyrix's Uc8179X4ProDriver::initController plus the
// full-refresh power/LUT setup its display() path issues. bb_epaper's UC81xx
// refresh appends DTM/DRF and POFF itself, so this table stops after PON.
static const uint8_t kUc8179Init[] PROGMEM = {
    EPD_RESET,
    BUSY_WAIT,
    3, 0x00, 0x3F, 0x0A,                   // panel setting
    5, 0x61, 0x03, 0x20, 0x02, 0x58,       // 800x600 addressed resolution
    5, 0x65, 0x00, 0x00, 0x00, 0x00,       // gate/source start
    2, 0x03, 0x20,                         // power-off sequence
    5, 0x06, 0x25, 0x25, 0x3C, 0x25,       // booster soft start
    2, 0xE1, 0x02,                         // gate scan
    2, 0xE3, 0x22,                         // power save
    1, 0x04,                               // PON
    BUSY_WAIT,
    2, 0x50, 0x97,                         // VCOM/data interval
    2, 0xE0, 0x02,                         // CCSET
    2, 0xE5, 0x1E,                         // TSSET
    3, 0x00, 0x3F, 0x0A,                   // re-assert panel setting before DRF
    0,
};

// UC8279 full init. The X4C omits the PLL (0x30) command the X4 Pro programs.
static const uint8_t kUc8279Init[] PROGMEM = {
    EPD_RESET,
    BUSY_WAIT,
    3, 0x00, 0x37, 0x4D,                   // panel setting
    5, 0x61, 0x03, 0x20, 0x02, 0x58,       // 800x600 addressed resolution
    5, 0x65, 0x00, 0x00, 0x00, 0x00,       // gate/source start
    2, 0x03, 0x20,                         // power-off sequence
    2, 0xE1, 0x02,                         // gate scan
    1, 0x04,                               // PON
    BUSY_WAIT,
    2, 0x50, 0x97,                         // VCOM/data interval
    2, 0xE0, 0x02,                         // CCSET
    2, 0xE5, 0x1E,                         // TSSET
    3, 0x00, 0x17, 0x4D,                   // OTP panel setting used before DRF
    0,
};

void x4c_apply_panel_overrides(BBEPAPER &bbep, X4cPanel panel) {
  if (panel == X4cPanel::SSD1677) return; // EP426 profile is already the right one

  const uint8_t *init = (panel == X4cPanel::UC8179) ? kUc8179Init : kUc8279Init;
  bbep._bbep.pInitFull = init;
  bbep._bbep.pInitFast = init;
  bbep._bbep.pInitPart = init;
  bbep._bbep.chip_type = BBEP_CHIP_UC81xx;
  bbep._bbep.iFlags = 0; // 1-bit only; the X4C has no color/gray panel modes yet
}

#endif // !PARALLEL_EPD
