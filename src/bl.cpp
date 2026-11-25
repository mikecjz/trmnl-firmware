#include <Arduino.h>
#include <ArduinoLog.h>
#include <app_logger.h>
#include <bl.h>
#include <display.h>
#include <display_session.h>
#include <fetch_timing.h>
#include <globals.h>
#include <request_handling.h>
#include <setup_session.h>
#include <sleep_session.h>
#include <status_screens.h>
#include <trmnl_log.h>
#include <wake_init.h>
#include <wifi_session.h>
#ifdef BOARD_XTEINK_X4_CLASSIC
#include <x4c_refresh_keys.h>
#endif

#ifdef BOARD_TRMNL_X
#include <touchbar_actions.h>
#endif

#ifdef BOARD_XTEINK_X4_CLASSIC
// Manual refreshes one wake will run for presses made while it was awake.
#define X4C_MAX_MANUAL_REFRESH_ROUNDS 2
#endif

// Fetches the screen from /api/display, shows it (or the error), then handles what the response asks for
static void showScreen(bool should_show_error_now) {
  https_request_err_e request_result = downloadAndShow(battery_voltage);
  esp_log_write(ESP_LOG_WARN, "X4C", "showScreen: request result %d\n", (int)request_result);
  Log.info("%s [%d]: request result - %s\r\n", __FILE__, __LINE__, https_request_err_str(request_result));
  showImageTooBigIfNeeded(request_result);
  applyRetryPolicy(request_result, should_show_error_now); // may sleep
  submitStoredLogs();
  showFriendlyIdIfUnregistered(request_result);
  resetIfRequested(request_result);
  updateFirmwareIfDue();
  showRequestResult(request_result);
}

/**
 * @brief Function to init business logic module
 * @param none
 * @return none
 */
void bl_init(void) {
  uint32_t init_time = bootEarly();
  wifiSessionInit();
  initPeripherals();
  WakeContext wake = readWakeContext();
  openPreferences();
  fetchTimingOnWake();
  startBoardHardware(wake, init_time);
#ifdef X4C_LOGO_SKIP_PROBE
  // Diagnostic build only: take the manual-refresh path on this boot so the
  // no-logo/single-draw behaviour can be checked without a physical key press.
  force_full_refresh = true;
#endif
  // A manual refresh (the X4C refresh keys) goes straight to the fetch: the logo
  // is an intermediate screen the user does not want to see. Power wakes and cold
  // boots keep it, and the full-refresh force is untouched.
  if (wake.reason != ESP_SLEEP_WAKEUP_TIMER && !force_full_refresh) showWakeLogo();
  logVersions();

  wifiSessionConnect(wake.showErrorsNow);
  updateTimeSinceSleep();
  ensureDeviceSetUp(); // may sleep
  submitStoredLogs();
  log_retry = true;

#ifdef BOARD_XTEINK_X4_CLASSIC
  // A refresh key pressed while this wake was booting or joining Wi-Fi: make
  // this wake's fetch the manual one rather than dropping the press.
  if (x4c_refresh_keys_take_press() && !force_full_refresh) {
    force_full_refresh = true;
    esp_log_write(ESP_LOG_WARN, "X4C", "refresh key pressed while awake: this fetch is the manual refresh\n");
  }
#endif

  // OTA checking, image checking and drawing
  showScreen(wake.showErrorsNow);
#ifdef BOARD_XTEINK_X4_CLASSIC
  // A refresh key pressed during the fetch or the draw: Wi-Fi is still up, so
  // run the manual refresh now. Bounded so a stream of presses cannot hold the
  // device awake; a press after the last round is caught again at sleep entry.
  for (int round = 0; round < X4C_MAX_MANUAL_REFRESH_ROUNDS && x4c_refresh_keys_take_press(); round++) {
    esp_log_write(ESP_LOG_WARN, "X4C", "refresh key pressed during the fetch: manual refresh now\n");
    force_full_refresh = true;
    showScreen(true);
  }
#endif
#ifdef BOARD_TRMNL_X
  touchbar_stay_awake([] { showScreen(true); });
#endif

  // display go to sleep
  Log_info("%s [%d]: BL done, going to sleep...", __FILE__, __LINE__);
  display_sleep();
  goToSleepOnSchedule();
} /* bl_init() */

/**
 * @brief Function to process business logic module
 * @param none
 * @return none
 */
void bl_process(void) {}
