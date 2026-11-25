#include <x4c_refresh_keys.h>

#ifdef BOARD_XTEINK_X4_CLASSIC

#include <Arduino.h>
#include <config.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_rtc_time.h>
#include <key_press_latch.h>

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

  KeyPressLatch s_latches[kKeyCount];
  portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
  TaskHandle_t s_task = nullptr;

  // The first press since the last take: when (RTC ms) and which keys. Written
  // by the task inside s_lock.
  uint32_t s_press_rtc_ms = 0;
  uint16_t s_press_mask = 0;
  // Per-wake diagnostics (RAM: one wake).
  uint16_t s_low_samples = 0;
  uint16_t s_task_presses = 0;
  uint16_t s_low_at_start = 0;

  // Survives deep sleep: set when a press arrived too late to act on in the
  // wake it happened in, with that press's time and keys.
  RTC_DATA_ATTR bool s_deferred = false;
  RTC_DATA_ATTR X4cKeyPress s_deferred_press = {0, 0};

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
          if (s_press_mask == 0) s_press_rtc_ms = now_ms;
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
  for (size_t i = 0; i < kKeyCount; i++) {
    pinMode(kKeys[i], INPUT_PULLUP);
    const bool low = key_low(kKeys[i]);
    keyPressLatchInit(s_latches[i], low);
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
  const X4cKeyPress seen = {s_press_rtc_ms, s_press_mask};
  s_press_rtc_ms = 0;
  s_press_mask = 0;
  taskEXIT_CRITICAL(&s_lock);
  if (pressed && press) *press = seen;
  return pressed;
}

bool x4c_refresh_keys_take_press_before_sleep(X4cKeyPress *press) {
  if (s_task == nullptr) return false; // not watching: nothing to compare against
  X4cKeyPress seen = {0, 0};
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

void x4c_refresh_keys_defer_to_next_wake(const X4cKeyPress &press) {
  s_deferred = true;
  s_deferred_press = press;
}

bool x4c_refresh_keys_take_deferred(X4cKeyPress *press) {
  const bool deferred = s_deferred;
  s_deferred = false;
  if (deferred && press) *press = s_deferred_press;
  return deferred;
}

uint16_t x4c_refresh_keys_low_samples(void) { return s_low_samples; }

uint16_t x4c_refresh_keys_task_presses(void) { return s_task_presses; }

uint16_t x4c_refresh_keys_low_at_start(void) { return s_low_at_start; }

#endif // BOARD_XTEINK_X4_CLASSIC
