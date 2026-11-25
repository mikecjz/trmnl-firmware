#include <fetch_schedule.h>
#include <stdio.h>
#include <unity.h>

static const uint64_t MS = 1000ULL;
static const uint64_t S = 1000000ULL;

// --- Grid arithmetic ------------------------------------------------------

void test_first_cycle_starts_the_grid_at_the_wake() {
  FetchSchedule schedule = {};
  const FetchWake wake = fetchScheduleOnWake(schedule, 100 * S, false);

  TEST_ASSERT_FALSE(wake.scheduled);
  TEST_ASSERT_TRUE(schedule.anchored);
  TEST_ASSERT_EQUAL_UINT64(100 * S, schedule.due_us);

  // 3 s into a 10 s cycle: 7 s left on the timer, and the next point is 10 s out.
  TEST_ASSERT_EQUAL_UINT64(7 * S, fetchScheduleSleepUs(schedule, 103 * S, 10 * S));
  TEST_ASSERT_EQUAL_UINT64(110 * S, schedule.due_us);
}

void test_scheduled_wake_reports_its_drift() {
  FetchSchedule schedule = {110 * S, true};
  const FetchWake wake = fetchScheduleOnWake(schedule, 110 * S + 250 * MS, true);

  TEST_ASSERT_TRUE(wake.scheduled);
  TEST_ASSERT_EQUAL_INT64(250 * MS, wake.drift_us);
}

void test_scheduled_sleep_takes_the_awake_time_out_of_the_sleep() {
  FetchSchedule schedule = {110 * S, true};
  const uint64_t wake = 110 * S + 250 * MS;
  fetchScheduleOnWake(schedule, wake, true);

  // 4 s awake (250 ms of it late against the grid) -> 5.75 s of sleep, not 10 s.
  TEST_ASSERT_EQUAL_UINT64(5750 * MS, fetchScheduleSleepUs(schedule, wake + 4 * S, 10 * S));
  TEST_ASSERT_EQUAL_UINT64(120 * S, schedule.due_us);
}

void test_drift_does_not_accumulate_over_cycles() {
  const uint64_t awake = 4 * S;
  FetchSchedule schedule = {100 * S, true};
  uint64_t wake = 100 * S;

  for (int cycle = 1; cycle <= 20; cycle++) {
    const FetchWake woken = fetchScheduleOnWake(schedule, wake, true);
    TEST_ASSERT_TRUE(woken.scheduled);
    TEST_ASSERT_EQUAL_INT64(0, woken.drift_us);

    const uint64_t next_wake = wake + awake + fetchScheduleSleepUs(schedule, wake + awake, 10 * S);
    TEST_ASSERT_EQUAL_UINT64(wake + 10 * S, next_wake);        // period stays the interval
    TEST_ASSERT_EQUAL_UINT64(wake + 10 * S, schedule.due_us);  // and the grid does not shift
    wake = next_wake;
  }
}

void test_a_late_wake_shifts_the_sleep_but_not_the_grid() {
  // The sleep timer overshoots by 150 ms every cycle (RTC oscillator error).
  const uint64_t awake = 4 * S;
  FetchSchedule schedule = {100 * S, true};
  uint64_t wake = 100 * S + 150 * MS;

  for (int cycle = 1; cycle <= 20; cycle++) {
    const FetchWake woken = fetchScheduleOnWake(schedule, wake, true);
    TEST_ASSERT_TRUE(woken.scheduled);
    TEST_ASSERT_EQUAL_INT64(150 * MS, woken.drift_us); // the same lateness every cycle

    const uint64_t sleep = fetchScheduleSleepUs(schedule, wake + awake, 10 * S);
    TEST_ASSERT_EQUAL_UINT64(10 * S - awake - 150 * MS, sleep);
    wake = wake + awake + sleep + 150 * MS; // next timer wake is late again
  }
}

void test_a_manual_wake_starts_a_new_grid() {
  FetchSchedule schedule = {110 * S, true};
  const FetchWake wake = fetchScheduleOnWake(schedule, 103 * S, false); // button, while asleep

  TEST_ASSERT_FALSE(wake.scheduled);
  TEST_ASSERT_EQUAL_UINT64(103 * S, schedule.due_us);
}

void test_an_overrunning_cycle_sleeps_only_the_minimum() {
  FetchSchedule schedule = {110 * S, true};
  fetchScheduleOnWake(schedule, 110 * S, true);

  // 11 s awake on a 10 s interval: the grid point has already gone, so take the
  // floor rather than adding a whole interval on top of the overrun.
  TEST_ASSERT_EQUAL_UINT64(FETCH_SCHEDULE_MIN_SLEEP_US, fetchScheduleSleepUs(schedule, 121 * S, 10 * S));
  TEST_ASSERT_EQUAL_UINT64(121 * S + FETCH_SCHEDULE_MIN_SLEEP_US, schedule.due_us);
}

void test_a_grid_point_too_close_still_gets_the_minimum_sleep() {
  FetchSchedule schedule = {110 * S, true};
  fetchScheduleOnWake(schedule, 110 * S, true);

  // 9.5 s awake: 0.5 s to the grid point is below the floor.
  TEST_ASSERT_EQUAL_UINT64(FETCH_SCHEDULE_MIN_SLEEP_US,
                           fetchScheduleSleepUs(schedule, 119 * S + 500 * MS, 10 * S));
  TEST_ASSERT_EQUAL_UINT64(120 * S + 500 * MS, schedule.due_us);
}

void test_rtc_memory_anchor_is_dropped_when_the_rtc_counter_restarted() {
  // The RTC memory survives a reset, the RTC counter does not.
  FetchSchedule schedule = {100 * S, true};
  const FetchWake wake = fetchScheduleOnWake(schedule, 1 * S, true);

  TEST_ASSERT_FALSE(wake.scheduled);
  TEST_ASSERT_EQUAL_UINT64(1 * S, schedule.due_us);
}

void test_backoff_sleep_restarts_the_grid() {
  FetchSchedule schedule = {110 * S, true};
  fetchScheduleRestart(schedule, 60 * S);

  const FetchWake wake = fetchScheduleOnWake(schedule, 60 * S + 200 * MS, true);
  TEST_ASSERT_TRUE(wake.scheduled);
  TEST_ASSERT_EQUAL_INT64(200 * MS, wake.drift_us);
  TEST_ASSERT_EQUAL_UINT64(6 * S, fetchScheduleSleepUs(schedule, 64 * S, 10 * S));
}

void test_zero_interval_does_not_spin() {
  FetchSchedule schedule = {};
  TEST_ASSERT_EQUAL_UINT64(0, fetchScheduleSleepUs(schedule, 42 * S, 0));
}

void test_a_cycle_longer_than_the_interval_sleeps_only_the_floor() {
  // What the X4C actually does at a 10 s configured interval: ~15.1 s awake.
  // The period cannot be shorter than the cycle, but it should be the cycle
  // plus the floor, not the cycle plus a whole interval.
  const uint64_t awake = 15100 * MS;
  const uint64_t interval = 10 * S;
  FetchSchedule schedule = {};
  fetchScheduleOnWake(schedule, 0, false);

  TEST_ASSERT_EQUAL_UINT64(FETCH_SCHEDULE_MIN_SLEEP_US, fetchScheduleSleepUs(schedule, awake, interval));
  TEST_ASSERT_EQUAL_UINT64(awake + FETCH_SCHEDULE_MIN_SLEEP_US, schedule.due_us);
  printf("overrun cycle (awake %llu ms, configured %llu ms): period %llu ms instead of %llu ms\n",
         (unsigned long long)(awake / 1000), (unsigned long long)(interval / 1000),
         (unsigned long long)((awake + FETCH_SCHEDULE_MIN_SLEEP_US) / 1000),
         (unsigned long long)((awake + interval) / 1000));
}

// --- Before/after measurement ---------------------------------------------

namespace {

const uint64_t INTERVAL_US = 10 * S; // the configured refresh interval
const uint64_t PRE_FETCH_MS = 2400;  // boot + Wi-Fi association before /api/display

// Awake time of each cycle: boot, Wi-Fi, request, decode and panel update. The
// values vary the way they do on a real device; that variation is what a full
// interval on top of the cycle turns into growing drift.
const uint64_t AWAKE_MS[] = {3200, 4100, 3600, 5000, 3300, 6200, 3900, 4400, 3500, 4800};
const int CYCLES = 10;

struct Cycle {
  uint64_t fetch_us;
  uint64_t period_us;
};

// Drives the same fetchSchedule functions the firmware uses. The clock is
// virtual and the sleep timer is exact, so this measures the schedule maths
// rather than the RTC oscillator; the device log lines measure the real thing.
void simulate(bool compensated, Cycle out[CYCLES]) {
  FetchSchedule schedule = {};
  uint64_t now = 0; // boot time of the cycle
  uint64_t previous_fetch = 0;

  for (int i = 0; i < CYCLES; i++) {
    if (compensated) fetchScheduleOnWake(schedule, now, i > 0);

    const uint64_t fetch = now + PRE_FETCH_MS * MS;
    out[i].fetch_us = fetch;
    out[i].period_us = i == 0 ? 0 : fetch - previous_fetch;
    previous_fetch = fetch;

    const uint64_t sleep_at = now + AWAKE_MS[i] * MS;
    const uint64_t sleep = compensated ? fetchScheduleSleepUs(schedule, sleep_at, INTERVAL_US) : INTERVAL_US;
    now = sleep_at + sleep;
  }
}

} // namespace

void test_before_and_after_measured_against_a_10_second_interval() {
  Cycle before[CYCLES];
  Cycle after[CYCLES];
  simulate(false, before); // old behaviour: a full interval after the cycle
  simulate(true, after);   // fetchScheduleSleepUs()

  printf("\n=== fetch period, configured %llu ms (host simulation, exact timer) ===\n",
         (unsigned long long)(INTERVAL_US / 1000));
  printf("%5s | %12s %10s %10s | %12s %10s %10s\n", "cycle", "before fetch", "period", "drift", "after fetch",
         "period", "drift");
  for (int i = 0; i < CYCLES; i++) {
    const long long before_drift = (long long)((int64_t)before[i].period_us - (int64_t)INTERVAL_US) / 1000;
    const long long after_drift = (long long)((int64_t)after[i].period_us - (int64_t)INTERVAL_US) / 1000;
    printf("%5d | %12llu %10llu %10lld | %12llu %10llu %10lld\n", i + 1,
           (unsigned long long)(before[i].fetch_us / 1000), (unsigned long long)(before[i].period_us / 1000),
           before_drift, (unsigned long long)(after[i].fetch_us / 1000),
           (unsigned long long)(after[i].period_us / 1000), after_drift);
  }

  uint64_t before_total = 0;
  uint64_t after_total = 0;
  for (int i = 1; i < CYCLES; i++) {
    before_total += before[i].period_us;
    after_total += after[i].period_us;
    // The old code adds one cycle's awake time to every period...
    TEST_ASSERT_EQUAL_UINT64(INTERVAL_US + AWAKE_MS[i - 1] * MS, before[i].period_us);
    // ...the schedule makes the period the configured interval.
    TEST_ASSERT_EQUAL_UINT64(INTERVAL_US, after[i].period_us);
  }
  printf("mean period over %d cycles: before %llu ms, after %llu ms (configured %llu ms)\n", CYCLES - 1,
         (unsigned long long)(before_total / (CYCLES - 1) / 1000),
         (unsigned long long)(after_total / (CYCLES - 1) / 1000),
         (unsigned long long)(INTERVAL_US / 1000));
  printf("drift after %d cycles: before +%lld ms, after %lld ms\n\n", CYCLES - 1,
         (long long)((before_total - (uint64_t)(CYCLES - 1) * INTERVAL_US) / 1000),
         (long long)(((int64_t)after_total - (int64_t)(CYCLES - 1) * (int64_t)INTERVAL_US) / 1000));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_first_cycle_starts_the_grid_at_the_wake);
  RUN_TEST(test_scheduled_wake_reports_its_drift);
  RUN_TEST(test_scheduled_sleep_takes_the_awake_time_out_of_the_sleep);
  RUN_TEST(test_drift_does_not_accumulate_over_cycles);
  RUN_TEST(test_a_late_wake_shifts_the_sleep_but_not_the_grid);
  RUN_TEST(test_a_manual_wake_starts_a_new_grid);
  RUN_TEST(test_an_overrunning_cycle_sleeps_only_the_minimum);
  RUN_TEST(test_a_grid_point_too_close_still_gets_the_minimum_sleep);
  RUN_TEST(test_rtc_memory_anchor_is_dropped_when_the_rtc_counter_restarted);
  RUN_TEST(test_backoff_sleep_restarts_the_grid);
  RUN_TEST(test_zero_interval_does_not_spin);
  RUN_TEST(test_a_cycle_longer_than_the_interval_sleeps_only_the_floor);
  RUN_TEST(test_before_and_after_measured_against_a_10_second_interval);
  return UNITY_END();
}
