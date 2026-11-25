#include <fetch_schedule.h>

FetchWake fetchScheduleOnWake(FetchSchedule &schedule, uint64_t now_us, bool timer_wake) {
  FetchWake result = {false, 0};

  // A usable anchor has to come from this power-on: the RTC counter restarts at
  // 0 on a reset even though the RTC memory (and so the anchor) survives it.
  const bool anchor_valid = schedule.anchored && now_us >= schedule.due_us;
  if (!timer_wake || !anchor_valid) {
    schedule.due_us = now_us; // button/power-on wake: start a new grid here
    schedule.anchored = true;
    return result;
  }

  result.scheduled = true;
  result.drift_us = (int64_t)(now_us - schedule.due_us);
  return result;
}

uint64_t fetchScheduleSleepUs(FetchSchedule &schedule, uint64_t now_us, uint64_t interval_us) {
  if (interval_us == 0) {
    schedule.due_us = now_us;
    schedule.anchored = true;
    return 0;
  }
  if (!schedule.anchored) {
    schedule.due_us = now_us; // no anchor: this cycle starts the grid
    schedule.anchored = true;
  }

  uint64_t next = schedule.due_us + interval_us;
  if (next < now_us + FETCH_SCHEDULE_MIN_SLEEP_US) {
    // The cycle already used up the interval (or is about to). Sleeping a whole
    // extra interval would move the device further from the configured rate, so
    // take the floor instead: as close to the rate as the hardware can get.
    next = now_us + FETCH_SCHEDULE_MIN_SLEEP_US;
  }

  schedule.due_us = next;
  return next - now_us;
}

void fetchScheduleRestart(FetchSchedule &schedule, uint64_t now_us) {
  schedule.due_us = now_us;
  schedule.anchored = true;
}
