#pragma once

#include <stdint.h>

// Time-based full (flashing) refresh schedule.
//
// The X4 Classic used to take a full OTP flash on every 8th paint, which ties
// the flash rate to how often the content changes rather than to time: a
// 5-second fetch flashed every 40 s, a once-a-day fetch went 8 days between
// clears. This schedule replaces that with "flash once the glass has gone
// `full_refresh_interval` seconds since its last flash", plus a hard cap on
// consecutive partials so ghosting stays bounded when the clock cannot help.
//
// Kept free of Arduino/ESP-IDF dependencies so the behaviour is unit-tested on
// the host; the firmware wrapper (src/x4c_full_refresh.cpp) only adds RTC
// storage and the system clock.

// Cadence used when the server sends no `full_refresh_interval`, or 0.
#define FULL_REFRESH_INTERVAL_DEFAULT_S       3600

// Upper bound accepted from the server: a day is the longest cadence that still
// clears the panel daily. Anything larger is treated as "as rare as allowed".
#define FULL_REFRESH_INTERVAL_MAX_S           86400

// Safety cap on consecutive non-flashing paints. It is only a ghosting backstop:
// at the 3600 s default a 5-minute refresh paints ~12 times an hour, so 64 is
// more than five hours of headroom and can never bind in normal operation. It
// exists for the cases the clock cannot cover - a fast poll (5 s) or a cold boot
// whose clock never syncs - where an unbounded partial streak is the one thing
// that definitely ghosts. 64 keeps the old "every 8 paints" ordering of
// magnitude while staying far enough above it not to defeat the time gate.
#define FULL_REFRESH_MAX_CONSECUTIVE_PARTIALS 64

struct FullRefreshState {
  uint32_t last_full_epoch;     // epoch of the last full flash; 0 = clock was unknown then
  uint32_t age_seconds;         // deep-sleep time accumulated since that flash
  uint32_t partials_since_full; // consecutive non-flashing paints
};

// Server value -> effective interval. 0 (or absent, parsed as 0) means the
// firmware default; oversized values are clamped.
uint32_t fullRefreshIntervalSeconds(uint32_t server_value);

// Seconds since the last full flash. The epoch is authoritative once both it
// and the last flash are known; before that (after a cold boot, until SNTP
// answers) the accumulated deep-sleep time stands in, so the gate never reads
// "overdue" just because the clock is not set yet.
uint32_t fullRefreshAgeSeconds(const FullRefreshState &state, uint32_t now_epoch);

// True when a full flash is due: the interval elapsed, or the partial cap hit.
bool fullRefreshIsDue(const FullRefreshState &state, uint32_t now_epoch, uint32_t server_value);

// Add one sleep to the fallback age. Called with the interval the deep-sleep
// timer was actually armed with; a button wake that cuts the sleep short
// overcounts, which only makes the next flash slightly early.
void fullRefreshNoteSleep(FullRefreshState &state, uint32_t sleep_seconds);

// A full flash happened: restart the interval and the partial streak. Pass the
// epoch read at the same moment; 0 keeps the accumulated sleep time in charge.
void fullRefreshNoteFlash(FullRefreshState &state, uint32_t now_epoch);

// A non-flashing paint completed.
void fullRefreshNotePartial(FullRefreshState &state);
