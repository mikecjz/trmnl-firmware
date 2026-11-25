#include <Arduino.h>
#include <fetch_schedule.h>
#include <fetch_timing.h>
#include <globals.h>
#include <refresh_interval.h>
#include <trmnl_log.h>

#include "esp_rtc_time.h"

namespace {

constexpr uint64_t US_PER_SECOND = 1000000ULL;

// The schedule has to outlive deep sleep, and the C runtime must not
// re-initialize it on the way back: FetchSchedule is a plain aggregate, so its
// zero initializer is constant and lives in .rtc.data untouched by a wake.
RTC_DATA_ATTR FetchSchedule fetchScheduleState = {};
RTC_DATA_ATTR uint64_t lastFetchStartUs = 0;
RTC_DATA_ATTR uint32_t fetchCount = 0;

uint64_t intervalUs() { return (uint64_t)refreshInterval.seconds() * US_PER_SECOND; }

} // namespace

void fetchTimingOnWake(void) {
  const uint64_t now = esp_rtc_get_time_us();
  const uint64_t interval = intervalUs();
  const FetchWake wake = fetchScheduleOnWake(fetchScheduleState, now, wakeup_reason == ESP_SLEEP_WAKEUP_TIMER);

  if (wake.scheduled)
    Log_info("fetch schedule: timer wake %lld ms after the scheduled point (rtc %llu ms, interval %llu s)",
             (long long)(wake.drift_us / 1000), (unsigned long long)(now / 1000),
             (unsigned long long)(interval / US_PER_SECOND));
  else
    Log_info("fetch schedule: new grid at rtc %llu ms (wake reason %d, interval %llu s)",
             (unsigned long long)(now / 1000), (int)wakeup_reason,
             (unsigned long long)(interval / US_PER_SECOND));
}

void fetchTimingOnFetchStart(void) {
  const uint64_t now = esp_rtc_get_time_us();
  const uint64_t configured_ms = intervalUs() / 1000;
  fetchCount++;

  if (lastFetchStartUs != 0 && now > lastFetchStartUs) {
    const uint64_t period_ms = (now - lastFetchStartUs) / 1000;
    const int64_t drift_ms = (int64_t)period_ms - (int64_t)configured_ms;
    Log_info("fetch %u start at rtc %llu ms: period %llu ms, configured %llu ms, drift %+lld ms",
             (unsigned)fetchCount, (unsigned long long)(now / 1000), (unsigned long long)period_ms,
             (unsigned long long)configured_ms, (long long)drift_ms);
  } else {
    Log_info("fetch %u start at rtc %llu ms: no previous fetch to measure against", (unsigned)fetchCount,
             (unsigned long long)(now / 1000));
  }
  lastFetchStartUs = now;
}

uint64_t fetchTimingScheduledSleepUs(void) {
  return fetchScheduleSleepUs(fetchScheduleState, esp_rtc_get_time_us(), intervalUs());
}

uint64_t fetchTimingBackoffSleepUs(void) {
  const uint64_t interval = intervalUs();
  fetchScheduleRestart(fetchScheduleState, esp_rtc_get_time_us() + interval);
  return interval;
}

void fetchTimingNoteArmedSleep(uint64_t sleep_us) {
  fetchScheduleRestart(fetchScheduleState, esp_rtc_get_time_us() + sleep_us);
}
