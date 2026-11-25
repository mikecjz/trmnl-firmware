#pragma once

#include <stdint.h>

#ifdef BOARD_XTEINK_X4_CLASSIC
#include <tap_trace.h>

// X4 Classic refresh-key ("tap") trace. Records every wake's phase timings, every
// refresh-key press (who caught it, when) and what each press led to (press ->
// fetch start, press -> full flash, request result) in a ring in RTC no-init
// memory (lib/trmnl tap_trace.h).
//
// Reading it: open the USB console. That resets the chip, and every boot that
// is not a deep-sleep wake prints the whole ring as `X4C: tap #...` lines,
// oldest first. Each wake also prints its own Cycle line at sleep entry, which a
// console that stays open sees live.
//
// It adds no network traffic and no flash writes, so it does not change the
// timings it measures.

// Call once per boot, after the wake reason is known and the key watcher runs.
void x4c_tap_trace_boot(void);

// A refresh-key press was taken. `rtc_ms` is when the key went down (or the
// wake time for a key wake).
void x4c_tap_trace_press(TapSource source, TapPhase phase, uint32_t rtc_ms, uint16_t key_mask);

// RTC milliseconds when this wake started (chip reset/wake).
uint32_t x4c_tap_trace_wake_rtc_ms(void);

// Phase marks for this wake.
void x4c_tap_trace_wifi_up(void);
void x4c_tap_trace_fetch_start(void);
void x4c_tap_trace_download_done(void);
void x4c_tap_trace_draw_done(bool flashed);
void x4c_tap_trace_result(int request_result);

// Deep sleep is about to start. `deferred_out`: a press was deferred to the
// next wake.
void x4c_tap_trace_sleep(uint32_t armed_sleep_ms, bool deferred_out);
#endif // BOARD_XTEINK_X4_CLASSIC
