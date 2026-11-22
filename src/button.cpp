#include "button.h"

#include <Arduino.h>
#include <ArduinoLog.h>
#include <WifiCaptive.h>
#include <bl.h>
#include <board_config.h>
#include <config.h>
#include <globals.h>
#include <misc/buzzer.h>

#include "trmnl_log.h"

#ifdef BOARD_XTEINK_X4_CLASSIC
#include <esp_log.h>
#include <esp_sleep.h>

// The mask of RTC GPIOs that triggered the ext1 wake. Zero when this boot was not
// a button wake (timer, cold boot), so the callers fall back to normal handling.
static uint64_t x4c_wake_key_mask(void) {
  if (wakeup_reason != ESP_SLEEP_WAKEUP_EXT1) {
    return 0;
  }
  return esp_sleep_get_ext1_wakeup_status();
}

// RTC memory survives deep sleep and a USB-triggered chip reset, so a console
// capture that resets the chip can still report the last natural wake.
RTC_DATA_ATTR static int x4c_last_wake_reason = 0;
RTC_DATA_ATTR static uint64_t x4c_last_wake_mask = 0;

bool button_wake_came_from_refresh_key(void) { return (x4c_wake_key_mask() & X4C_REFRESH_KEY_MASK) != 0; }

// A wake that was not a button wake at all counts as "not the refresh key", so the
// power-button press-duration menu still runs for cold boots and timer wakes.
bool button_wake_came_from_power_key(void) {
  const uint64_t mask = x4c_wake_key_mask();
  return mask == 0 || (mask & (1ULL << X4C_KEY_POWER_GPIO)) != 0;
}

void button_log_wake_source(void) {
  const uint64_t mask = x4c_wake_key_mask();
  // The previous boot's panel-update count is still in RTC memory. One update on
  // a refresh-key wake means the logo frame was skipped; two would mean it was
  // drawn and then covered by the dashboard.
  const uint32_t prev_draws = x4c_draw_count;
  x4c_draw_count = 0;
  esp_log_write(ESP_LOG_WARN, "X4C",
                "wake: reason=%d mask=0x%03llx prev_reason=%d prev_mask=0x%03llx prev_draws=%lu\n",
                (int)wakeup_reason, (unsigned long long)mask, x4c_last_wake_reason,
                (unsigned long long)x4c_last_wake_mask, (unsigned long)prev_draws);
  if (mask != 0) {
    x4c_last_wake_reason = (int)wakeup_reason;
    x4c_last_wake_mask = mask;
  }
}
#endif // BOARD_XTEINK_X4_CLASSIC

static unsigned long wait_for_button_release(unsigned long start_time) {
  pinMode(pDevice->interrupt_pin, INPUT);
  while (digitalRead(pDevice->interrupt_pin) == LOW && millis() - start_time < BUTTON_SOFT_RESET_TIME) {
    delay(10);
  }
  if (millis() - start_time >= BUTTON_SOFT_RESET_TIME) {
    buzzer().beepPattern(3, 100, 100);
  }
  return millis() - start_time;
}

static ButtonPressResult classify_press_duration(unsigned long duration) {
  if (duration >= BUTTON_SOFT_RESET_TIME) {
    Log_info("Button time=%lu detected extra-long press", duration);
    return SoftReset;
  } else if (duration > BUTTON_HOLD_TIME) {
    Log_info("Button time=%lu detected long press", duration);
    return LongPress;
  } else if (duration > BUTTON_MEDIUM_HOLD_TIME) {
    Log_info("Button time=%lu detected long press", duration);
    return DoubleClick;
  }
  return NoAction;
}

static ButtonPressResult wait_for_second_press(unsigned long start_time) {
  auto release_time = millis();

  while (millis() - release_time < BUTTON_DOUBLE_CLICK_WINDOW) {
    if (digitalRead(pDevice->interrupt_pin) == LOW) {
      auto second_press_start = millis();
      auto second_duration = wait_for_button_release(second_press_start);

      ButtonPressResult long_press_result = classify_press_duration(second_duration);
      if (long_press_result != NoAction) {
        return long_press_result;
      }

      Log_info("Button time=%lu detected double-click", millis() - start_time);
      return DoubleClick;
    }
    delay(10);
  }

  return ShortPress;
}

static ButtonPressResult classify_button_presses() {
  auto time_start = millis();
  Log_info("Button time=%lu: start", time_start);
  pinMode(pDevice->interrupt_pin, INPUT);
  if (digitalRead(pDevice->interrupt_pin) == HIGH) {
    if (time_start < 2000) {
      Log_info("Button: already released at start (GPIO wakeup), waiting for second press");
      return wait_for_second_press(time_start);
    } else {
      Log_info("Button: waiting for button press");
      while (digitalRead(pDevice->interrupt_pin) == HIGH) {
        delay(10);
      }
      time_start = millis();
    }
  }

  auto press_duration = wait_for_button_release(time_start);

  ButtonPressResult long_press_result = classify_press_duration(press_duration);
  if (long_press_result != NoAction) {
    return long_press_result;
  }

  if (press_duration > 50) {
    Log_info("Button: first press detected, waiting for second press");
    return wait_for_second_press(time_start);
  }

  return NoAction;
}

ButtonPressResult read_button_presses() {
  auto result = classify_button_presses();
  if (result == ShortPress || result == DoubleClick) {
    buzzer().beep(100);
  }
  return result;
}

// Acts on the press read after a button wake: a long press opens the settings portal (the saved WiFi
// networks are kept), a soft-reset press forgets the device (does not return). Returns true for a double click, which
// runs the special function.
bool button_wake_act(ButtonPressResult button) {
  bool double_click = false;
  Log_info("GPIO wakeup (%d) -> button was read (%s)", wakeup_reason, ButtonPressResultNames[button]);
  switch (button) {
  case LongPress:
    Log_info("Settings portal requested");
    WifiCaptivePortal.requestPortal();
    break;
  case DoubleClick:
    double_click = true;
    break;
  case ShortPress:
  case NoAction:
    break;
  case SoftReset:
    resetDeviceCredentials();
  }
  Log_info("button handling end");
  return double_click;
}

// Loads the special function the server assigned into special_function and opens the WiFi portal for
// SF_ADD_WIFI. The others run with the next /api/display request.
void button_wake_run_special_function(void) {
  if (preferences.isKey(PREFERENCES_SF_KEY)) {
    Log.info("%s [%d]: SF saved. Reading...\r\n", __FILE__, __LINE__);
    special_function = (SPECIAL_FUNCTION)preferences.getUInt(PREFERENCES_SF_KEY, 0);
    Log.info("%s [%d]: Read special function - %d\r\n", __FILE__, __LINE__, special_function);
    switch (special_function) {
    case SF_IDENTIFY: {
      Log.info("%s [%d]: Identify special function...It will be handled with API ping...\r\n", __FILE__, __LINE__);
    } break;
    case SF_SLEEP: {
      Log.info("%s [%d]: Sleep special function...\r\n", __FILE__, __LINE__);
        // still in progress
    } break;
    case SF_ADD_WIFI: {
      Log.info("%s [%d]: Add WiFi function...\r\n", __FILE__, __LINE__);
      WifiCaptivePortal.startPortal();
    } break;
    case SF_RESTART_PLAYLIST: {
      Log.info("%s [%d]: Restart Playlist special function...It will be handled with API ping...\r\n", __FILE__,
               __LINE__);
    } break;
    case SF_REWIND: {
      Log.info("%s [%d]: Rewind special function...\r\n", __FILE__, __LINE__);
    } break;
    case SF_SEND_TO_ME: {
      Log.info("%s [%d]: Send to me special function...It will be handled with API ping...\r\n", __FILE__, __LINE__);
    } break;
    case SF_GUEST_MODE: {
      Log.info("%s [%d]: Guest Mode special function...It will be handled with API ping...\r\n", __FILE__, __LINE__);
    } break;
    default:
      break;
    }
  } else {
    Log_error("SF not saved");
  }
}
