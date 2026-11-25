#include <x4c_refresh_keys.h>

#ifdef BOARD_XTEINK_X4_CLASSIC

#include <Arduino.h>
#include <config.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_log.h>
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

  // Survives deep sleep: set when a press arrived too late to act on in the
  // wake it happened in.
  RTC_DATA_ATTR bool s_deferred = false;

  bool key_low(uint8_t pin) { return gpio_get_level(static_cast<gpio_num_t>(pin)) == 0; }

  void watch_task(void *) {
    for (;;) {
      for (size_t i = 0; i < kKeyCount; i++) {
        const bool low = key_low(kKeys[i]);
        taskENTER_CRITICAL(&s_lock);
        keyPressLatchSample(s_latches[i], low);
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
    keyPressLatchInit(s_latches[i], key_low(kKeys[i]));
  }
  if (xTaskCreate(watch_task, "x4c_keys", kTaskStackBytes, nullptr, 1, &s_task) != pdPASS) {
    s_task = nullptr;
    esp_log_write(ESP_LOG_WARN, "X4C", "refresh key watcher not started\n");
  }
}

bool x4c_refresh_keys_take_press(void) {
  bool pressed = false;
  taskENTER_CRITICAL(&s_lock);
  for (size_t i = 0; i < kKeyCount; i++) pressed |= keyPressLatchTake(s_latches[i]);
  taskEXIT_CRITICAL(&s_lock);
  return pressed;
}

bool x4c_refresh_keys_take_press_before_sleep(void) {
  if (s_task == nullptr) return false; // not watching: nothing to compare against
  bool pressed = x4c_refresh_keys_take_press();
  for (size_t i = 0; i < kKeyCount; i++) {
    const bool low = key_low(kKeys[i]);
    taskENTER_CRITICAL(&s_lock);
    pressed |= keyPressLatchIsStarting(s_latches[i], low);
    taskEXIT_CRITICAL(&s_lock);
  }
  return pressed;
}

void x4c_refresh_keys_defer_to_next_wake(void) { s_deferred = true; }

bool x4c_refresh_keys_take_deferred(void) {
  const bool deferred = s_deferred;
  s_deferred = false;
  return deferred;
}

#endif // BOARD_XTEINK_X4_CLASSIC
