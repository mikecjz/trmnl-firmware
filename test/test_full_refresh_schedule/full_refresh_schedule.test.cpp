#include <full_refresh_schedule.h>
#include <unity.h>

static FullRefreshState fresh_state() {
  const FullRefreshState state = {0, 0, 0};
  return state;
}

static void test_interval_defaults_when_absent_or_zero(void) {
  TEST_ASSERT_EQUAL_UINT32(FULL_REFRESH_INTERVAL_DEFAULT_S, fullRefreshIntervalSeconds(0));
}

static void test_interval_passes_through_and_clamps(void) {
  TEST_ASSERT_EQUAL_UINT32(300, fullRefreshIntervalSeconds(300));
  TEST_ASSERT_EQUAL_UINT32(FULL_REFRESH_INTERVAL_MAX_S, fullRefreshIntervalSeconds(FULL_REFRESH_INTERVAL_MAX_S));
  TEST_ASSERT_EQUAL_UINT32(FULL_REFRESH_INTERVAL_MAX_S, fullRefreshIntervalSeconds(FULL_REFRESH_INTERVAL_MAX_S + 1));
}

// A cold boot has no clock and no base: it must not be "due" on every paint.
static void test_fresh_state_is_not_due(void) {
  FullRefreshState state = fresh_state();
  TEST_ASSERT_FALSE(fullRefreshIsDue(state, 0, 0));
  TEST_ASSERT_FALSE(fullRefreshIsDue(state, 0, 3600));
}

static void test_epoch_gate_fires_at_the_interval(void) {
  FullRefreshState state = fresh_state();
  const uint32_t flash_at = 1700000000UL;
  fullRefreshNoteFlash(state, flash_at);
  TEST_ASSERT_FALSE(fullRefreshIsDue(state, flash_at + 3599, 3600));
  TEST_ASSERT_TRUE(fullRefreshIsDue(state, flash_at + 3600, 3600));
}

// Before SNTP answers, elapsed time comes from the sleep intervals actually
// armed. 5-minute fetch: one flash per hour, i.e. the 12th sleep.
static void test_sleep_accumulation_drives_the_gate_without_a_clock(void) {
  FullRefreshState state = fresh_state();
  for (int i = 0; i < 11; i++) {
    fullRefreshNoteSleep(state, 300);
    fullRefreshNotePartial(state);
    TEST_ASSERT_FALSE(fullRefreshIsDue(state, 0, 0));
  }
  fullRefreshNoteSleep(state, 300);
  fullRefreshNotePartial(state);
  TEST_ASSERT_TRUE(fullRefreshIsDue(state, 0, 0));
}

// The epoch wins when it is available, and awake time is counted with it.
static void test_epoch_age_wins_over_sleep_accumulation(void) {
  FullRefreshState state = fresh_state();
  const uint32_t flash_at = 1700000000UL;
  fullRefreshNoteFlash(state, flash_at);
  fullRefreshNoteSleep(state, 300);
  // 1800 s later by the clock, 300 s in the accumulator: the clock is right.
  TEST_ASSERT_EQUAL_UINT32(1800, fullRefreshAgeSeconds(state, flash_at + 1800));
  TEST_ASSERT_TRUE(fullRefreshIsDue(state, flash_at + 1800, 1800));
}

// If the clock steps back past the last flash, the accumulator still bounds it.
static void test_clock_step_back_falls_back_to_accumulation(void) {
  FullRefreshState state = fresh_state();
  fullRefreshNoteFlash(state, 1700000000UL);
  for (int i = 0; i < 12; i++)
    fullRefreshNoteSleep(state, 300);
  TEST_ASSERT_TRUE(fullRefreshIsDue(state, 1600000000UL, 3600));
}

static void test_consecutive_partial_cap_bounds_ghosting(void) {
  FullRefreshState state = fresh_state();
  for (uint32_t i = 0; i < FULL_REFRESH_MAX_CONSECUTIVE_PARTIALS - 1; i++)
    fullRefreshNotePartial(state);
  TEST_ASSERT_FALSE(fullRefreshIsDue(state, 0, 86400));
  fullRefreshNotePartial(state);
  TEST_ASSERT_TRUE(fullRefreshIsDue(state, 0, 86400));
}

static void test_flash_restarts_interval_and_streak(void) {
  FullRefreshState state = fresh_state();
  fullRefreshNotePartial(state);
  fullRefreshNoteSleep(state, 900);
  fullRefreshNoteFlash(state, 1700000000UL);
  TEST_ASSERT_EQUAL_UINT32(1700000000UL, state.last_full_epoch);
  TEST_ASSERT_EQUAL_UINT32(0, state.age_seconds);
  TEST_ASSERT_EQUAL_UINT32(0, state.partials_since_full);
  TEST_ASSERT_FALSE(fullRefreshIsDue(state, 1700000000UL, 3600));
}

// A flash before the clock is set must leave the epoch unknown so the sleep
// accumulator keeps measuring elapsed time.
static void test_flash_without_a_clock_keeps_the_accumulator_in_charge(void) {
  FullRefreshState state = fresh_state();
  fullRefreshNoteSleep(state, 600);
  fullRefreshNoteFlash(state, 0);
  TEST_ASSERT_EQUAL_UINT32(0, state.last_full_epoch);
  TEST_ASSERT_EQUAL_UINT32(0, state.age_seconds);
  fullRefreshNoteSleep(state, 3600);
  TEST_ASSERT_TRUE(fullRefreshIsDue(state, 0, 3600));
}

// A server-side change applies to the very next evaluation, with no stored
// threshold to go stale.
static void test_interval_change_applies_immediately(void) {
  FullRefreshState state = fresh_state();
  fullRefreshNoteFlash(state, 1700000000UL);
  TEST_ASSERT_FALSE(fullRefreshIsDue(state, 1700001000UL, 3600));
  TEST_ASSERT_TRUE(fullRefreshIsDue(state, 1700001000UL, 900));
}

static void test_sleep_accumulator_saturates(void) {
  FullRefreshState state = fresh_state();
  fullRefreshNoteSleep(state, UINT32_MAX);
  fullRefreshNoteSleep(state, 1000);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, state.age_seconds);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_interval_defaults_when_absent_or_zero);
  RUN_TEST(test_interval_passes_through_and_clamps);
  RUN_TEST(test_fresh_state_is_not_due);
  RUN_TEST(test_epoch_gate_fires_at_the_interval);
  RUN_TEST(test_sleep_accumulation_drives_the_gate_without_a_clock);
  RUN_TEST(test_epoch_age_wins_over_sleep_accumulation);
  RUN_TEST(test_clock_step_back_falls_back_to_accumulation);
  RUN_TEST(test_consecutive_partial_cap_bounds_ghosting);
  RUN_TEST(test_flash_restarts_interval_and_streak);
  RUN_TEST(test_flash_without_a_clock_keeps_the_accumulator_in_charge);
  RUN_TEST(test_interval_change_applies_immediately);
  RUN_TEST(test_sleep_accumulator_saturates);
  return UNITY_END();
}
