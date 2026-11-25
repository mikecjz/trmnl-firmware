#include <x4c_tap_trace.h>

#ifdef BOARD_XTEINK_X4_CLASSIC

#include <esp_attr.h>
#include <esp_log.h>
#include <esp_rtc_time.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <globals.h>
#include <misc/clock.h>
#include <x4c_refresh_keys.h>

namespace {
  // Not initialized on any reset, so the ring survives deep sleep, a panic and
  // the chip reset the USB console triggers. tapTracePush() resets it when the
  // memory does not hold a ring yet (power-on garbage).
  RTC_NOINIT_ATTR TapTrace s_trace;

  // This wake, in RAM.
  uint32_t s_wake_rtc_ms = 0;
  uint32_t s_wifi_up_ms = TAP_PHASE_NONE;
  uint32_t s_fetch_start_ms = TAP_PHASE_NONE;
  uint32_t s_download_ms = TAP_PHASE_NONE;
  uint32_t s_draw_ms = TAP_PHASE_NONE;
  uint16_t s_ext1_mask = 0;
  uint8_t s_flags = 0;
  int s_last_result = 0;

  // The press this wake still owes a manual refresh for (the first one; later
  // presses before its fetch are merged into it).
  struct Pending {
    bool active;
    TapSource source;
    uint32_t press_rtc_ms;
    uint32_t fetch_rtc_ms; // 0 until its fetch starts
    bool second_fetch;
    uint32_t flash_rtc_ms; // 0 until the full flash for it; the record is written at the request result
  } s_pending = {};

  uint32_t rtc_now_ms() { return (uint32_t)(esp_rtc_get_time_us() / 1000ULL); }

  uint32_t since_wake_ms() { return rtc_now_ms() - s_wake_rtc_ms; }

  void print(const TapRecord &record, uint32_t seq) {
    char line[256];
    tapTraceFormat(line, sizeof(line), seq, record);
    esp_log_write(ESP_LOG_WARN, "X4C", "%s\n", line);
  }

  void push(const TapRecord &record) {
    tapTracePush(s_trace, record);
    print(record, s_trace.seq - 1);
  }

  void close_pending(uint32_t flash_rtc_ms) {
    if (!s_pending.active) return;
    const uint32_t fetch_delay =
      s_pending.fetch_rtc_ms != 0 ? s_pending.fetch_rtc_ms - s_pending.press_rtc_ms : UINT32_MAX;
    const uint32_t flash_delay = flash_rtc_ms != 0 ? flash_rtc_ms - s_pending.press_rtc_ms : UINT32_MAX;
    push(tapManualRecord(s_pending.press_rtc_ms, s_pending.source, (uint16_t)s_last_result, fetch_delay,
                         flash_delay, s_pending.second_fetch));
    s_pending = {};
  }
} // namespace

void x4c_tap_trace_boot(void) {
  // esp_timer starts with the app, so this misses the ROM + bootloader time
  // (about 0.35 s); every "ms since wake" below is measured from app start.
  s_wake_rtc_ms = rtc_now_ms() - (uint32_t)(esp_timer_get_time() / 1000);
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) s_ext1_mask = (uint16_t)esp_sleep_get_ext1_wakeup_status();

  if (esp_reset_reason() != ESP_RST_DEEPSLEEP && tapTraceIsValid(s_trace)) {
    esp_log_write(ESP_LOG_WARN, "X4C", "tap trace: %u records, oldest first (reset reason %d)\n",
                  (unsigned)s_trace.count, (int)esp_reset_reason());
    for (uint16_t i = 0; i < s_trace.count; i++) print(tapTraceAt(s_trace, i), tapTraceSeqAt(s_trace, i));
    esp_log_write(ESP_LOG_WARN, "X4C", "tap trace: end\n");
  }
  esp_log_write(ESP_LOG_WARN, "X4C", "tap trace: wake@%lums reason=%d ext1=0x%03x keys_low_at_start=0x%03x\n",
                (unsigned long)s_wake_rtc_ms, (int)wakeup_reason, (unsigned)s_ext1_mask,
                (unsigned)x4c_refresh_keys_low_at_start());
}

uint32_t x4c_tap_trace_wake_rtc_ms(void) { return s_wake_rtc_ms; }

void x4c_tap_trace_press(TapSource source, TapPhase phase, uint32_t rtc_ms, uint16_t key_mask) {
  push(tapPressRecord(rtc_ms, source, phase, key_mask, rtc_ms - s_wake_rtc_ms));
  if (phase == TapPhase::SleepEntry) return; // served by the next wake, which records it as deferred-wake
  if (phase == TapPhase::Wake && source == TapSource::DeferredWake) s_flags |= TAP_CYCLE_DEFERRED_IN;
  if (!s_pending.active) {
    // A press taken after the download, before its draw, is served by that
    // draw: its "fetch" is the one that just finished, with no wait.
    s_pending = {true, source, rtc_ms, phase == TapPhase::BeforeDraw ? rtc_ms : 0, phase == TapPhase::AfterFetch, 0};
  }
}

void x4c_tap_trace_wifi_up(void) {
  if (s_wifi_up_ms == TAP_PHASE_NONE) s_wifi_up_ms = since_wake_ms();
}

void x4c_tap_trace_fetch_start(void) {
  if (s_fetch_start_ms == TAP_PHASE_NONE) s_fetch_start_ms = since_wake_ms();
  // The manual fetch is the first one that starts after the press was taken
  // with the hard refresh requested.
  if (s_pending.active && s_pending.fetch_rtc_ms == 0 && force_full_refresh) s_pending.fetch_rtc_ms = rtc_now_ms();
}

void x4c_tap_trace_download_done(void) { s_download_ms = since_wake_ms(); }

void x4c_tap_trace_draw_done(bool flashed) {
  s_draw_ms = since_wake_ms();
  if (!flashed) return;
  s_flags |= TAP_CYCLE_FLASHED;
  if (s_pending.active && s_pending.fetch_rtc_ms != 0 && s_pending.flash_rtc_ms == 0) {
    s_flags |= TAP_CYCLE_MANUAL;
    s_pending.flash_rtc_ms = rtc_now_ms(); // the Manual record waits for the request result
  }
}

void x4c_tap_trace_result(int request_result) {
  s_last_result = request_result;
  if (s_pending.active && s_pending.flash_rtc_ms != 0) close_pending(s_pending.flash_rtc_ms);
}

void x4c_tap_trace_draw_failed(TapDrawFail reason) {
  s_flags |= TAP_CYCLE_DRAW_FAILED;
  push(tapDrawFailRecord(rtc_now_ms(), reason, since_wake_ms(),
                         (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                         (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

void x4c_tap_trace_wifi_path(bool fast_rejoin, bool lease) {
  if (fast_rejoin) s_flags |= TAP_CYCLE_FAST_REJOIN;
  if (lease) s_flags |= TAP_CYCLE_LEASE;
}

void x4c_tap_trace_sleep(uint32_t armed_sleep_ms, bool deferred_out) {
  close_pending(s_pending.flash_rtc_ms); // flashed but no result yet, or never flashed (0)
  if (deferred_out) s_flags |= TAP_CYCLE_DEFERRED_OUT;
  const uint32_t epoch = (uint32_t)systemClock().getTime();
  push(tapCycleRecord(s_wake_rtc_ms, (uint16_t)wakeup_reason, s_ext1_mask, s_flags, s_wifi_up_ms, s_fetch_start_ms,
                      s_download_ms, s_draw_ms, since_wake_ms(), armed_sleep_ms, epoch,
                      x4c_refresh_keys_low_samples(), x4c_refresh_keys_task_presses()));
}

#endif // BOARD_XTEINK_X4_CLASSIC
