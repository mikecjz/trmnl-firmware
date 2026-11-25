#pragma once

#include <stdint.h>

/**
 * Device side of the fetch schedule: RTC-backed state, the log lines used to
 * measure the real period, and the sleep durations sleep_session arms.
 *
 * The measurement lines are:
 *   "fetch schedule: ..."  once per boot, with how late the timer wake was
 *   "fetch N start: period ..."  at each /api/display fetch, with the period
 *                                since the previous fetch and its drift
 * Consecutive `start at rtc ...` values are directly diffable, so the actual
 * fetch-to-fetch period can be read without trusting the configured value.
 * They go to the ESP log console, so they do not depend on the server-side
 * verbose-logging setting.
 */

/// Call once per boot from bl_init(), after preferences are open.
void fetchTimingOnWake(void);

/// Call at the start of every /api/display fetch.
void fetchTimingOnFetchStart(void);

/// Sleep for the end of a normal cycle: the time left until the next scheduled
/// fetch, with the schedule advanced. Also takes the awake time out of the
/// sleep so the fetch-to-fetch period stays equal to the configured interval.
uint64_t fetchTimingScheduledSleepUs(void);

/// Sleep for an off-schedule cycle (retry/backoff): a full interval, after
/// which the fetch grid restarts from that wake.
uint64_t fetchTimingBackoffSleepUs(void);

/// Point the grid at the wake that was actually armed, for callers that shorten
/// the sleep (the TRMNL X keep-timer path).
void fetchTimingNoteArmedSleep(uint64_t sleep_us);
