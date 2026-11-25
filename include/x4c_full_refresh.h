#pragma once

#include <stdint.h>

#ifdef BOARD_XTEINK_X4_CLASSIC
// X4 Classic full-refresh gate: time since the last full flash, fed by the
// server's `full_refresh_interval`, with the RTC-backed clock as the primary
// source and the armed deep-sleep timers as the fallback before the first SNTP
// sync. The state lives in RTC memory, so it survives deep sleep; a cold boot
// clears it and the first paint of that boot is a flash, which re-bases it.
//
// See lib/trmnl/include/full_refresh_schedule.h for the tested logic.

// True when this paint should be a full OTP flash. Reads the clock itself.
bool x4c_full_refresh_due(uint32_t server_interval_seconds);

// Seconds since the last full flash (for logging).
uint32_t x4c_full_refresh_age_seconds(void);

// A full flash completed. Call only after the panel update succeeded.
void x4c_full_refresh_note_flash(void);

// A non-flashing paint completed.
void x4c_full_refresh_note_partial(void);

// Deep sleep is about to be armed for `sleep_seconds`; add it to the fallback
// age so elapsed time is still measured when the clock is not set.
void x4c_full_refresh_note_sleep(uint32_t sleep_seconds);
#endif // BOARD_XTEINK_X4_CLASSIC
