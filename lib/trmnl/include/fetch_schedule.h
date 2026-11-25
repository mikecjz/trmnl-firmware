#pragma once

#include <stdint.h>

/**
 * Keeps the time between fetches equal to the configured refresh interval.
 *
 * A wake costs more than the request itself: the device has to boot and bring
 * up Wi-Fi before it can fetch. Arming the deep-sleep timer with the full
 * interval *after* that work makes every period interval + awake time, so the
 * device polls later than configured by one whole cycle's overhead, every
 * cycle. Instead this keeps a grid in a clock that keeps running across deep
 * sleep (esp_rtc_get_time_us() on the device): the timer is armed for the time
 * left until the next grid point, so the awake time is taken out of the sleep
 * instead of being added to the period.
 *
 * Times are microseconds of that clock. The struct is meant to live in RTC
 * memory, so it stays a plain aggregate: value-initialize it once and let the
 * functions below advance it.
 */
struct FetchSchedule {
  /// RTC time the current cycle was scheduled to start at.
  uint64_t due_us;
  /// False until a grid exists (first boot) and after the anchor is discarded.
  bool anchored;
};

struct FetchWake {
  /// True when this boot was the deep-sleep timer waking at a grid point.
  bool scheduled;
  /// How late the wake was against the grid; 0 when it was not a scheduled wake.
  int64_t drift_us;
};

/// Shortest sleep a scheduled cycle will arm. Below this a cycle costs more
/// energy than the extra fetch is worth.
static constexpr uint64_t FETCH_SCHEDULE_MIN_SLEEP_US = 1000000ULL; // 1 s

/// Call once per boot, as early as possible. A timer wake keeps the grid and
/// reports its drift; any other wake (button, power-on, reset) starts a new
/// grid at `now_us`.
FetchWake fetchScheduleOnWake(FetchSchedule &schedule, uint64_t now_us, bool timer_wake);

/// Call at the end of a normal cycle: advances the grid and returns the time to
/// arm the deep-sleep timer with. The result is never below
/// FETCH_SCHEDULE_MIN_SLEEP_US, whatever the interval. A cycle longer than the
/// interval sleeps only the floor: the device cannot fetch faster than one
/// cycle takes, so an extra whole interval would only widen the gap.
uint64_t fetchScheduleSleepUs(FetchSchedule &schedule, uint64_t now_us, uint64_t interval_us);

/// Start a new grid at `now_us`, e.g. after an off-schedule (retry) sleep.
void fetchScheduleRestart(FetchSchedule &schedule, uint64_t now_us);
