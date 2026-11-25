#include <x4c_refresh_keys.h>

#ifdef BOARD_XTEINK_X4_CLASSIC

#include <Arduino.h>
#include <config.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_rtc_time.h>
#include <esp_sleep.h>
#include <globals.h>
#include <key_press_latch.h>
#include <x4c_tap_trace.h>

namespace {
  constexpr uint8_t kKeys[] = {X4C_KEY_LEFT_GPIO, X4C_KEY_RIGHT_GPIO, X4C_KEY_CONFIRM_GPIO, X4C_KEY_BACK_GPIO};
  constexpr size_t kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);
  // 10 ms samples with a 2-sample debounce: a tap of ~20 ms or more is caught.
  constexpr TickType_t kSamplePeriod = pdMS_TO_TICKS(10);
  // The task only samples GPIO levels and updates the latches: it must never
  // log. esp_log_write() goes through esp_insights and newlib's vfprintf
  // (800-byte frame, re-entered through __sbprintf for an unbuffered console),
  // which overflowed the original 2 KB stack on the first press and
  // panic-reset the chip. The margin here is for interrupt frames.
  constexpr uint32_t kTaskStackBytes = 3072;
  // A key that is already down when sampling starts and comes up within this
  // many samples (1 s) was tapped during the boot window, before the task ran.
  constexpr uint16_t kBootGraceSamples = 100;

  KeyPressLatch s_latches[kKeyCount];
  portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
  TaskHandle_t s_task = nullptr;

  // The first press since the last take: when (RTC ms) and which keys. Written
  // by the task inside s_lock.
  uint32_t s_press_rtc_ms = 0;
  uint16_t s_press_mask = 0;
  bool s_press_boot_grace = false;
  // Per-wake diagnostics (RAM: one wake).
  uint16_t s_low_samples = 0;
  uint16_t s_task_presses = 0;
  uint16_t s_low_at_start = 0;

  // Survives deep sleep: set when a press arrived too late to act on in the
  // wake it happened in, with that press's time and keys.
  RTC_DATA_ATTR bool s_deferred = false;
  RTC_DATA_ATTR X4cKeyPress s_deferred_press = {0, 0, false};
  RTC_DATA_ATTR uint8_t s_deferred_attempt = 0;

  // When sampling started, for a press found through the boot grace.
  uint32_t s_watch_start_rtc_ms = 0;

  // The press this wake owes a manual refresh (RAM: one wake). `attempt` is how
  // many wakes have already tried it.
  bool s_owed = false;
  X4cKeyPress s_owed_press = {0, 0, false};
  uint8_t s_owed_attempt = 0;

  uint32_t rtc_now_ms() { return (uint32_t)(esp_rtc_get_time_us() / 1000ULL); }

  bool key_low(uint8_t pin) { return gpio_get_level(static_cast<gpio_num_t>(pin)) == 0; }

  void watch_task(void *) {
    for (;;) {
      // Read outside the lock: the RTC clock read is not trivially short.
      const uint32_t now_ms = rtc_now_ms();
      for (size_t i = 0; i < kKeyCount; i++) {
        const bool low = key_low(kKeys[i]);
        taskENTER_CRITICAL(&s_lock);
        if (low && s_low_samples < UINT16_MAX) s_low_samples++;
        if (keyPressLatchSample(s_latches[i], low)) {
          if (s_task_presses < UINT16_MAX) s_task_presses++;
          // A press completed by a release is a boot-window tap: it happened
          // before sampling started.
          const bool grace = !s_latches[i].pressed;
          if (s_press_mask == 0) {
            s_press_rtc_ms = grace ? s_watch_start_rtc_ms : now_ms;
            s_press_boot_grace = grace;
          }
          s_press_mask |= (uint16_t)(1u << kKeys[i]);
        }
        taskEXIT_CRITICAL(&s_lock);
      }
      vTaskDelay(kSamplePeriod);
    }
  }
} // namespace

void x4c_refresh_keys_watch_start(void) {
  if (s_task != nullptr) return;
  s_watch_start_rtc_ms = rtc_now_ms();
  // Keys that already have a press being served this wake must not count again
  // when they come up: the key that woke the chip (ext1), and the key a
  // deferred press was taken from (it can still be down 100 ms later).
  uint64_t served = s_deferred ? s_deferred_press.key_mask : 0;
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) served |= esp_sleep_get_ext1_wakeup_status();
  for (size_t i = 0; i < kKeyCount; i++) {
    pinMode(kKeys[i], INPUT_PULLUP);
    const bool low = key_low(kKeys[i]);
    const bool was_served = (served & (1ULL << kKeys[i])) != 0;
    keyPressLatchInitWithGrace(s_latches[i], low, was_served ? 0 : kBootGraceSamples);
    if (low) s_low_at_start |= (uint16_t)(1u << kKeys[i]);
  }
  if (xTaskCreate(watch_task, "x4c_keys", kTaskStackBytes, nullptr, 1, &s_task) != pdPASS) {
    s_task = nullptr;
    esp_log_write(ESP_LOG_WARN, "X4C", "refresh key watcher not started\n");
  }
}

bool x4c_refresh_keys_take_press(X4cKeyPress *press) {
  bool pressed = false;
  taskENTER_CRITICAL(&s_lock);
  for (size_t i = 0; i < kKeyCount; i++) pressed |= keyPressLatchTake(s_latches[i]);
  const X4cKeyPress seen = {s_press_rtc_ms, s_press_mask, s_press_boot_grace};
  s_press_rtc_ms = 0;
  s_press_mask = 0;
  s_press_boot_grace = false;
  taskEXIT_CRITICAL(&s_lock);
  if (pressed && press) *press = seen;
  return pressed;
}

bool x4c_refresh_keys_take_press_before_sleep(X4cKeyPress *press) {
  if (s_task == nullptr) return false; // not watching: nothing to compare against
  X4cKeyPress seen = {0, 0, false};
  bool pressed = x4c_refresh_keys_take_press(&seen);
  for (size_t i = 0; i < kKeyCount; i++) {
    const bool low = key_low(kKeys[i]);
    taskENTER_CRITICAL(&s_lock);
    const bool starting = keyPressLatchIsStarting(s_latches[i], low);
    taskEXIT_CRITICAL(&s_lock);
    if (starting) {
      if (!pressed) seen.rtc_ms = rtc_now_ms();
      seen.key_mask |= (uint16_t)(1u << kKeys[i]);
      pressed = true;
    }
  }
  if (pressed && press) *press = seen;
  return pressed;
}

void x4c_refresh_keys_defer_to_next_wake(const X4cKeyPress &press, uint8_t attempt) {
  s_deferred = true;
  s_deferred_press = press;
  s_deferred_attempt = attempt;
}

bool x4c_refresh_keys_take_deferred(X4cKeyPress *press, uint8_t *attempt) {
  const bool deferred = s_deferred;
  s_deferred = false;
  if (deferred && press) *press = s_deferred_press;
  if (deferred && attempt) *attempt = s_deferred_attempt;
  return deferred;
}

void x4c_refresh_keys_owe(const X4cKeyPress &press, uint8_t attempt) {
  if (s_owed) return; // later presses merge into the one already owed
  s_owed = true;
  s_owed_press = press;
  s_owed_attempt = attempt;
}

void x4c_refresh_keys_settle(void) { s_owed = false; }

bool x4c_refresh_keys_take_owed(X4cKeyPress *press, uint8_t *attempt) {
  const bool owed = s_owed;
  s_owed = false;
  if (owed && press) *press = s_owed_press;
  if (owed && attempt) *attempt = s_owed_attempt;
  return owed;
}

void x4c_refresh_keys_check_before_draw(void) {
  X4cKeyPress press = {0, 0, false};
  if (!x4c_refresh_keys_take_press(&press)) return;
  x4c_tap_trace_press(press.boot_grace ? TapSource::BootGrace : TapSource::Task, TapPhase::BeforeDraw, press.rtc_ms,
                      press.key_mask);
  x4c_refresh_keys_owe(press, 0);
  if (!force_full_refresh) {
    force_full_refresh = true;
    esp_log_write(ESP_LOG_WARN, "X4C", "refresh key pressed before the draw: this draw is the manual refresh\n");
  }
}

uint16_t x4c_refresh_keys_low_samples(void) { return s_low_samples; }

uint16_t x4c_refresh_keys_task_presses(void) { return s_task_presses; }

uint16_t x4c_refresh_keys_low_at_start(void) { return s_low_at_start; }

#endif // BOARD_XTEINK_X4_CLASSIC
