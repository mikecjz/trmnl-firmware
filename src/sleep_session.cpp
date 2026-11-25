#include <Arduino.h>
#include <ArduinoLog.h>
#include <WiFi.h>
#include <app_logger.h>
#include <bl.h>
#include <board_config.h>
#include <config.h>
#include <display.h>
#include <fetch_timing.h>
#include <filesystem.h>
#include <globals.h>
#include <misc/clock.h>
#include <pins.h>
#include <refresh_interval.h>
#include <sleep_duration.h>
#include <sleep_session.h>
#include <status_screens.h>
#include <trmnl_log.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#ifdef BOARD_TRMNL_X
#include <iqs323_task.h>
#include <touchbar_gesture.h>

#include "esp_rtc_time.h"
#include "rtc_wake_stub_trmnl_x.h"
#endif

#ifdef BOARD_XTEINK_X4_CLASSIC
// The X4 Classic's setup portal stays open for 30 minutes (see PORTAL_TIMEOUT in
// WifiCaptive.cpp); this guard must sit above it so an unattended portal times
// out and sleeps instead of aborting first.
#define AWAKE_TIME_LIMIT_MS (40 * 60 * 1000)
#else
#define AWAKE_TIME_LIMIT_MS (20 * 60 * 1000) // longer than the 15 minute Wi-Fi portal
#endif

#ifdef BOARD_TRMNL_X
#define KEPT_TIMER_MIN_SLEEP_US (1 * SLEEP_uS_TO_S_FACTOR) // if due while preparing for sleep

// RTC time of the last sleep's timer, 0 if none
RTC_DATA_ATTR static uint64_t next_timer_wake_rtc_us = 0;
#endif

// Whether this sleep ends a normal cycle (so the next fetch keeps the schedule
// grid) or is an off-schedule backoff (retry, Wi-Fi failure, portal).
enum class SleepKind { Backoff, Scheduled };

// The time left until the next scheduled fetch for a normal cycle, the refresh
// interval for a backoff, or (keep_timer) the time left on the last sleep's
// timer. A scheduled cycle's awake time is taken out of the sleep, so it does
// not lengthen the fetch-to-fetch period (see fetch_schedule.h).
static uint64_t timerSleepUs([[maybe_unused]] bool keep_timer, SleepKind kind) {
  uint64_t sleep_us =
    kind == SleepKind::Scheduled ? fetchTimingScheduledSleepUs() : fetchTimingBackoffSleepUs();
#ifdef BOARD_TRMNL_X
  const uint64_t now = esp_rtc_get_time_us();
  if (keep_timer && next_timer_wake_rtc_us != 0) {
    const uint64_t remaining = next_timer_wake_rtc_us > now ? next_timer_wake_rtc_us - now : 0;
    sleep_us = min(sleep_us, max(remaining, (uint64_t)KEPT_TIMER_MIN_SLEEP_US));
    fetchTimingNoteArmedSleep(sleep_us);
  }
  next_timer_wake_rtc_us = now + sleep_us;
#endif
  return sleep_us;
}

/**
 * @brief Function to sleep preparing and go to sleep
 * @param keep_timer sleep until the last sleep's timer, not for the refresh interval
 * @param kind scheduled for a normal cycle, backoff for a retry
 * @return none
 */
#ifdef BOARD_XTEINK_X4_CLASSIC
// Arms the X4 Classic wake keys for deep sleep. Keys that read low are left out
// of the ext1 mask: a key that is held down (stuck, or pressed by a stand) stays
// low after the chip sleeps, so ext1 would wake it again immediately and turn
// every sleep into a logo/dashboard boot loop. Each pin is logged so a capture
// taken through the sleep shows the levels that were armed.
static void arm_x4c_wake_keys(void) {
  static const uint8_t keys[] = {
      X4C_KEY_POWER_GPIO, X4C_KEY_LEFT_GPIO, X4C_KEY_RIGHT_GPIO, X4C_KEY_CONFIRM_GPIO, X4C_KEY_BACK_GPIO};
  for (uint8_t pin : keys) {
    pinMode(pin, INPUT_PULLUP);
    // The digital pull-up is lost with the digital domain in deep sleep; the RTC
    // pad pull-up is what keeps a released key high while the chip sleeps.
    rtc_gpio_pullup_en((gpio_num_t)pin);
    rtc_gpio_pulldown_dis((gpio_num_t)pin);
  }

  uint64_t wake_mask = 0;
  for (uint8_t pin : keys) {
    if (digitalRead(pin) == HIGH) {
      wake_mask |= 1ULL << pin;
    }
  }
  esp_log_write(ESP_LOG_WARN, "X4C", "sleep: keys p3=%d p5=%d p2=%d p8=%d p9=%d mask=0x%03llx draws=%lu\n",
                digitalRead(X4C_KEY_POWER_GPIO), digitalRead(X4C_KEY_LEFT_GPIO), digitalRead(X4C_KEY_RIGHT_GPIO),
                digitalRead(X4C_KEY_CONFIRM_GPIO), digitalRead(X4C_KEY_BACK_GPIO), (unsigned long long)wake_mask,
                (unsigned long)x4c_draw_count);

  if (wake_mask == 0) {
    esp_log_write(ESP_LOG_WARN, "X4C", "sleep: every wake key is held low, timer wake only\n");
    return;
  }
  esp_sleep_enable_ext1_wakeup_io(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW);
}
#endif // BOARD_XTEINK_X4_CLASSIC

static void enterSleep(bool keep_timer, SleepKind kind = SleepKind::Backoff) {
  Log.info("%s [%d]: go to sleep\r\n", __FILE__, __LINE__);
  submitStoredLogs();

// DEBUG - workaround to prevent crash in the WiFi stack of unknown origin
#ifndef PARALLEL_EPD
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect();
  }
  WiFi.mode(WIFI_OFF);
#endif

#if BOARD_TRMNL_X
  Log_info("Preparing IQS323 for sleep via task...");

  // Use task manager for sleep preparation (sets event mode, checks I2C health)
  if (!iqs323_task_prepare_sleep(5000)) {
    Log.warning("IQS323 sleep preparation timeout - proceeding anyway\n");
  }

  touchbar_prepare_for_sleep();

  // Cleanup the task before entering deep sleep
  iqs323_task_deinit();
  Log_info("IQS323 is ready for sleep.");

  esp_set_deep_sleep_wake_stub(*wakeup_stub);
  display_sleep();
  config_tca95535_pins_for_lp();
  config_gpio_for_lp();
#endif

  filesystem_deinit();
  const uint64_t sleep_us = timerSleepUs(keep_timer, kind);
  uint32_t time_to_sleep = sleep_us / SLEEP_uS_TO_S_FACTOR;
  iPrevWakeTime = millis() - startup_time; // save for statistics
  Log.info("%s [%d]: total awake time - %d ms\r\n", __FILE__, __LINE__, iPrevWakeTime);
  Log.info("%s [%d]: time to sleep - %d\r\n", __FILE__, __LINE__, time_to_sleep);
  if (!keep_timer) preferences.putUInt(PREFERENCES_LAST_SLEEP_TIME, systemClock().getTime());
  preferences.end();
  esp_sleep_enable_timer_wakeup(sleep_us);
  // Configure GPIO pin for wakeup
#if CONFIG_IDF_TARGET_ESP32
#define BUTTON_PIN_BITMASK(GPIO) (1ULL << GPIO)  // 2 ^ GPIO_NUMBER in hex
  esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK(pDevice->interrupt_pin), ESP_EXT1_WAKEUP_ALL_LOW);
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C5)
  pinMode(pDevice->interrupt_pin, INPUT); // needed to not immediately wake up
  esp_deep_sleep_enable_gpio_wakeup(1 << pDevice->interrupt_pin, ESP_GPIO_WAKEUP_GPIO_LOW);
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#ifdef BOARD_TRMNL_X
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_INTERRUPT, 0);
#elif defined(BOARD_XTEINK_X4_CLASSIC)
  arm_x4c_wake_keys();
#else
  esp_sleep_enable_ext0_wakeup((gpio_num_t)pDevice->interrupt_pin, 0);
#endif
#else
#error "Unsupported ESP32 target for GPIO wakeup configuration"
#endif
#if defined(BOARD_XTEINK_X4) || defined(BOARD_XTEINK_X3)
// The Xteink X4 has a high current draw in deep sleep (3-4mA), so allow the user to select
// if they want to completely shut down the power and only update with a physical button press
// or have short battery life (5-7 days) in the normal TRMNL wakeup mode
#ifdef X4_WAKE_ON_BUTTON
  pinMode(13, OUTPUT);
  digitalWrite(13, 0); // cut off the battery power
  delay(100); // allow it to settle before going into power-off
#else // Keep the battery power on and allow timed wakeup
  gpio_hold_en(GPIO_NUM_13); // MOSFET enabling the battery power
  gpio_deep_sleep_hold_en(); // Needed to keep the battery power enabled during RTC sleep
#endif
#endif
#ifdef BOARD_XTEINK_X4_CLASSIC
  // GPIO1 is the X4 Classic's master peripheral/panel rail (CrossPoint's freeink-sdk
  // X4C profile, Papyrix's x4c power latch). Hold it HIGH across deep sleep so the
  // timer wakeup and the power-button wakeup both still work. Sleep current with the
  // rail held has not been measured yet; that is a hardware milestone.
  pinMode(1, OUTPUT);
  digitalWrite(1, HIGH);
  gpio_hold_en(GPIO_NUM_1);
  gpio_deep_sleep_hold_en();
#endif // BOARD_XTEINK_X4_CLASSIC
#ifdef BOARD_ZECTRIX_NOTE4
  digitalWrite(6, LOW); // disable the EPD power
  gpio_hold_en(GPIO_NUM_17); // MOSFET enabling the battery power
  gpio_deep_sleep_hold_en(); // Needed to keep the battery power enabled during RTC sleep
#endif // BOARD_ZECTRIX_NOTE4C

  esp_deep_sleep_start();
}

void goToSleep(void) { enterSleep(false); }

void goToSleepOnSchedule(void) {
  // The end of a normal cycle: keep the fetch grid, so the period is the
  // configured interval rather than interval + awake time.
  enterSleep(false, SleepKind::Scheduled);
}

#ifdef BOARD_TRMNL_X
void goToSleepKeepingTimer(void) {
  if (next_timer_wake_rtc_us != 0 && esp_rtc_get_time_us() >= next_timer_wake_rtc_us) {
    Log_info("Sleep timer already due - not sleeping");
    return;
  }
  enterSleep(true);
}
#endif

void updateTimeSinceSleep(void) {
  if (systemClock().setTimeFromNTP()) {
    const uint32_t last_sleep = preferences.getUInt(PREFERENCES_LAST_SLEEP_TIME, 0);
    time_since_sleep = sleepDurationSeconds(systemClock().getTime(), last_sleep);
  } else {
    time_since_sleep = 0;
    Log.info("%s [%d]: Time wasn't synced.\r\n", __FILE__, __LINE__);
  }

  Log.info("%s [%d]: Time since last sleep: %d\r\n", __FILE__, __LINE__, time_since_sleep);
}

void goToSleepButtonOnly(void) {
  submitStoredLogs();
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect();
  }
  WiFi.mode(WIFI_OFF);
  filesystem_deinit();
  preferences.end();
#if CONFIG_IDF_TARGET_ESP32
#define BUTTON_PIN_BITMASK_BTN(GPIO) (1ULL << GPIO)
  esp_sleep_enable_ext1_wakeup(BUTTON_PIN_BITMASK_BTN(pDevice->interrupt_pin), ESP_EXT1_WAKEUP_ALL_LOW);
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C5)
  esp_deep_sleep_enable_gpio_wakeup(1 << pDevice->interrupt_pin, ESP_GPIO_WAKEUP_GPIO_LOW);
#elif CONFIG_IDF_TARGET_ESP32S3
#ifdef BOARD_TRMNL_X
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_INTERRUPT, 0);
#elif defined(BOARD_XTEINK_X4_CLASSIC)
  arm_x4c_wake_keys();
#else
  esp_sleep_enable_ext0_wakeup((gpio_num_t)pDevice->interrupt_pin, 0);
#endif
#else
#error "Unsupported ESP32 target for GPIO wakeup configuration"
#endif
#ifdef BOARD_XTEINK_X4
  gpio_hold_en(GPIO_NUM_13);
  gpio_deep_sleep_hold_en();
#endif
#ifdef BOARD_XTEINK_X4_CLASSIC
  pinMode(1, OUTPUT);
  digitalWrite(1, HIGH);
  gpio_hold_en(GPIO_NUM_1);
  gpio_deep_sleep_hold_en();
#endif
  esp_deep_sleep_start();
}

void config_gpio_for_lp() {

#ifdef BOARD_TRMNL_X
  // XCL
  pinMode(GPIO_NUM_4, INPUT);

  // Data pins (d8 to d15)
  pinMode(GPIO_NUM_5, INPUT);
  pinMode(GPIO_NUM_6, INPUT);
  pinMode(GPIO_NUM_7, INPUT);
  pinMode(GPIO_NUM_15, INPUT);
  pinMode(GPIO_NUM_16, INPUT);
  pinMode(GPIO_NUM_17, INPUT);
  pinMode(GPIO_NUM_18, INPUT);
  pinMode(GPIO_NUM_8, INPUT);

  // D+ D-
  // pinMode(GPIO_NUM_19, INPUT);
  // pinMode(GPIO_NUM_20, INPUT);

  // Data pins (d0 to d7)
  pinMode(GPIO_NUM_9, INPUT);
  pinMode(GPIO_NUM_10, INPUT);
  pinMode(GPIO_NUM_11, INPUT);
  pinMode(GPIO_NUM_12, INPUT);
  pinMode(GPIO_NUM_13, INPUT);
  pinMode(GPIO_NUM_14, INPUT);
  pinMode(GPIO_NUM_21, INPUT);
  pinMode(GPIO_NUM_47, INPUT);

  // EP_STV
  pinMode(GPIO_NUM_48, INPUT);

  // CKV
  pinMode(GPIO_NUM_45, INPUT);

  // BTN1
  pinMode(GPIO_NUM_0, INPUT);

  // I2C
  pinMode(GPIO_NUM_39, INPUT); // SDA
  pinMode(GPIO_NUM_40, INPUT); // SCL

  // XSTL
  pinMode(GPIO_NUM_41, INPUT);

  // LEH
  pinMode(GPIO_NUM_42, INPUT);

  // UART0
  pinMode(GPIO_NUM_43, INPUT); // TXD
  pinMode(GPIO_NUM_44, INPUT); // RXD
  pinMode(GPIO_NUM_1, INPUT); // CTS
  pinMode(GPIO_NUM_2, INPUT); // RTS
#endif // BOARD_TRMNL_X
} /* config_gpio_for_lp() */

void wifiErrorDeepSleep(void) {
  if (!preferences.isKey(PREFERENCES_CONNECT_WIFI_RETRY_COUNT)) {
    preferences.putInt(PREFERENCES_CONNECT_WIFI_RETRY_COUNT, 1);
  }

  uint8_t retry_count = preferences.getInt(PREFERENCES_CONNECT_WIFI_RETRY_COUNT);

  Log_info("WIFI connection failed! Retry count: %d \n", retry_count);

  refreshInterval.applyWifiRetry(retry_count);

  if (retry_count >= MAX_QUIET_SLOW_RETRIES) {
    preferences.putInt(PREFERENCES_CONNECT_WIFI_RETRY_COUNT, 1);
    showMessageWithLogo(WIFI_FAILED);
    display_sleep();
    goToSleep();
    return;
  }
  retry_count++;
  preferences.putInt(PREFERENCES_CONNECT_WIFI_RETRY_COUNT, retry_count);

  display_sleep();
  goToSleep();
}

// A panic reset, so the next boot can report it.
static void awake_time_limit_reached(void *arg) { esp_system_abort("Awake time limit reached"); }

void awake_time_limit_start(void) {
  esp_timer_handle_t timer;
  esp_timer_create_args_t timerArgs = {};

  timerArgs.callback = awake_time_limit_reached;
  timerArgs.name = "awake_limit";
  if (esp_timer_create(&timerArgs, &timer) == ESP_OK) {
    esp_timer_start_once(timer, (uint64_t)AWAKE_TIME_LIMIT_MS * 1000);
  } else {
    Log_error("Awake time limit timer not created");
  }
}
