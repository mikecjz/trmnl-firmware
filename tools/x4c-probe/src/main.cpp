// Xteink X4 Classic (X4 v2) panel-controller probe.
//
// Read-only with respect to the panel: power the rail, reset, issue 0x70, clock
// three bytes back on MOSI (the board has no MISO). No panel init, no LUT or
// waveform write, no framebuffer. Flashed app-only at 0x10000 over app0; the
// stock bootloader, partition table and NVS are untouched.
//
// The sequence and timing mirror Papyrix's X4ClassicDisplayProbe.cpp and
// CrossPoint's freeink-sdk XteinkDetect, both validated against stock X4C
// firmware. VER byte 2 identifies the controller:
//   0x01, 0x40                      -> UC8179
//   0x02, 0x03, 0x41, 0x42,
//   0x67, 0x68, 0x69                -> UC8279
// anything else, or a uniform/floating bus, is inconclusive.

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <stdarg.h>
#include <string.h>

namespace {

  constexpr int PIN_SCLK = 12;
  constexpr int PIN_MOSI = 11;  // controller SDA in half-duplex read mode
  constexpr int PIN_CS = 13;
  constexpr int PIN_DC = 14;
  constexpr int PIN_RST = 10;
  constexpr int PIN_BUSY = 18;
  constexpr int PIN_PANEL_POWER = 1;  // master peripheral/panel rail, active HIGH

  constexpr uint8_t kCmdVer = 0x70;
  constexpr uint32_t kBusyTimeoutMs = 300;

  struct ProbeResult {
    bool completed;  // 0x70 was issued and all three bytes clocked
    bool busyTimedOut;
    uint32_t busyWaitMs;
    uint8_t ver[3];
  };

  void bothBegin() { Serial0.begin(115200); }

  void logf(const char *fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
  // ESP_LOG writes straight to the boot console (USB-Serial/JTAG here).
  // Arduino's HWCDC drops output until it has seen a host drain, and the IDF
  // usb_serial_jtag driver is not installed by the Arduino core.
    esp_log_write(ESP_LOG_ERROR, "PROBE", "%s\n", line);
    Serial0.println(line);
    Serial.println(line);
  }

  void releasePins() {
    pinMode(PIN_SCLK, INPUT);
    pinMode(PIN_MOSI, INPUT);
    pinMode(PIN_CS, INPUT_PULLUP);
    pinMode(PIN_DC, INPUT);
    pinMode(PIN_RST, INPUT);
    pinMode(PIN_BUSY, INPUT);
  }

  ProbeResult probeOnce() {
    ProbeResult r{};
    pinMode(PIN_CS, OUTPUT);
    digitalWrite(PIN_CS, HIGH);
    pinMode(PIN_SCLK, OUTPUT);
    digitalWrite(PIN_SCLK, LOW);
    pinMode(PIN_DC, OUTPUT);
    digitalWrite(PIN_DC, LOW);
    pinMode(PIN_MOSI, OUTPUT);
    pinMode(PIN_BUSY, INPUT);

    gpio_hold_dis(static_cast<gpio_num_t>(PIN_RST));
    pinMode(PIN_RST, OUTPUT);
    digitalWrite(PIN_RST, HIGH);
    delay(10);
    digitalWrite(PIN_RST, LOW);
    delay(50);
    digitalWrite(PIN_RST, HIGH);

    const uint32_t start = millis();
    while (digitalRead(PIN_BUSY) == LOW) {
      if (millis() - start >= kBusyTimeoutMs) {
        r.busyWaitMs = millis() - start;
        r.busyTimedOut = true;
        releasePins();
        return r;
      }
      delay(1);
    }
    r.busyWaitMs = millis() - start;

    digitalWrite(PIN_CS, LOW);
    uint8_t command = kCmdVer;
    for (unsigned bit = 0; bit < 8; ++bit) {
      digitalWrite(PIN_MOSI, (command & 0x80) ? HIGH : LOW);
      delayMicroseconds(1);
      digitalWrite(PIN_SCLK, HIGH);
      delayMicroseconds(1);
      digitalWrite(PIN_SCLK, LOW);
      command <<= 1;
    }

    digitalWrite(PIN_DC, HIGH);
    pinMode(PIN_MOSI, INPUT);
    for (unsigned idx = 0; idx < 3; ++idx) {
      uint8_t byte = 0;
      for (unsigned bit = 0; bit < 8; ++bit) {
        delayMicroseconds(1);
        digitalWrite(PIN_SCLK, HIGH);
        delayMicroseconds(1);
        byte = static_cast<uint8_t>((byte << 1) | (digitalRead(PIN_MOSI) == HIGH));
        digitalWrite(PIN_SCLK, LOW);
      }
      r.ver[idx] = byte;
    }
    digitalWrite(PIN_CS, HIGH);
    releasePins();
    r.completed = true;
    return r;
  }

  const char *controllerFor(uint8_t id) {
    switch (id) {
    case 0x01:
    case 0x40:
      return "UC8179";
    case 0x02:
    case 0x03:
    case 0x41:
    case 0x42:
    case 0x67:
    case 0x68:
    case 0x69:
      return "UC8279";
    default:
      return "UNKNOWN";
    }
  }

  bool floating(const uint8_t v[3]) {
    return (v[0] == 0x00 && v[1] == 0x00 && v[2] == 0x00) || (v[0] == 0xFF && v[1] == 0xFF && v[2] == 0xFF);
  }

  void powerRailUp() {
    pinMode(PIN_PANEL_POWER, OUTPUT);
    digitalWrite(PIN_PANEL_POWER, HIGH);
  // Stock drives this rail HIGH at boot; give the controller time to come up.
    delay(150);
  }

  ProbeResult g_last{};

  void runPass(unsigned n) {
    const char *label = n ? "pass" : "rerun";
    g_last = probeOnce();
    if (g_last.busyTimedOut) {
      logf("PROBE %s%u: BUSY timeout after %u ms, no 0x70 issued", label, n, static_cast<unsigned>(g_last.busyWaitMs));
    } else if (!g_last.completed) {
      logf("PROBE %s%u: failed before read", label, n);
    } else {
      logf("PROBE %s%u: busy=%u ms VER=%02X %02X %02X id=0x%02X -> %s%s", label, n,
           static_cast<unsigned>(g_last.busyWaitMs), g_last.ver[0], g_last.ver[1], g_last.ver[2], g_last.ver[2],
           floating(g_last.ver) ? "FLOATING" : controllerFor(g_last.ver[2]),
           floating(g_last.ver) ? " (uniform bus, inconclusive)" : "");
    }
  }

}  // namespace

void setup() {
  bothBegin();
  pinMode(PIN_PANEL_POWER, OUTPUT);
  digitalWrite(PIN_PANEL_POWER, HIGH);

  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 4000)
    delay(10);  // wait for a USB host

#if defined(ARDUINO_USB_CDC_ON_BOOT)
  // A positive timeout is required: HWCDC::flush treats 0 as "CDC gone" and
  // discards the TX ring buffer on every call.
  Serial.setTxTimeoutMs(1000);
#endif

  logf("");
  logf("=== X4C PROBE-ONLY build (reset + 0x70 read; no panel init) ===");
  logf("chip=%s rev=%u cores=%u", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());
  logf("efuse-mac=%012llX flash=%lu MHz", ESP.getEfuseMac(),
       static_cast<unsigned long>(ESP.getFlashChipSpeed() / 1000000));
  logf("reset reason=%d", static_cast<int>(esp_reset_reason()));
  logf("pins: sclk=%d mosi=%d cs=%d dc=%d rst=%d busy=%d power=%d", PIN_SCLK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST,
       PIN_BUSY, PIN_PANEL_POWER);

  powerRailUp();
  for (unsigned pass = 1; pass <= 5; ++pass) {
    runPass(pass);
    delay(150);
  }

  if (!g_last.completed) {
    logf("PROBE_RESULT controller=INCONCLUSIVE (probe did not complete)");
  } else if (floating(g_last.ver)) {
    logf("PROBE_RESULT controller=INCONCLUSIVE (floating bus)");
  } else if (strcmp(controllerFor(g_last.ver[2]), "UNKNOWN") == 0) {
    logf("PROBE_RESULT controller=INCONCLUSIVE id=0x%02X (unrecognised)", g_last.ver[2]);
  } else {
    logf("PROBE_RESULT controller=%s id=0x%02X", controllerFor(g_last.ver[2]), g_last.ver[2]);
  }
  logf("=== probe complete; repeating every 10 s ===");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last >= 10000) {
    last = millis();
    powerRailUp();
    runPass(0);
    if (g_last.completed && !floating(g_last.ver)) {
      logf("PROBE_RESULT controller=%s id=0x%02X", controllerFor(g_last.ver[2]), g_last.ver[2]);
    } else {
      logf("PROBE_RESULT controller=INCONCLUSIVE");
    }
  }
  delay(20);
}
