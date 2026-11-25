#include <string.h>
#include <tap_trace.h>
#include <unity.h>

static TapTrace trace;

void setUp() { memset(&trace, 0xA5, sizeof(trace)); } // RTC no-init memory starts as garbage

void test_garbage_memory_is_not_a_valid_ring() { TEST_ASSERT_FALSE(tapTraceIsValid(trace)); }

void test_push_on_garbage_starts_a_clean_ring() {
  tapTracePush(trace, tapPressRecord(100, TapSource::Task, TapPhase::BeforeFetch, 0x20, 50));

  TEST_ASSERT_TRUE(tapTraceIsValid(trace));
  TEST_ASSERT_EQUAL_UINT16(1, trace.count);
  TEST_ASSERT_EQUAL_UINT32(0, tapTraceSeqAt(trace, 0));
  TEST_ASSERT_EQUAL_UINT32(100, tapTraceAt(trace, 0).t_ms);
}

void test_ring_keeps_the_newest_records_in_order() {
  tapTraceReset(trace);
  const uint32_t total = TAP_TRACE_CAPACITY + 10;
  for (uint32_t i = 0; i < total; i++)
    tapTracePush(trace, tapPressRecord(i, TapSource::Task, TapPhase::BeforeFetch, 0, 0));

  TEST_ASSERT_EQUAL_UINT16(TAP_TRACE_CAPACITY, trace.count);
  TEST_ASSERT_EQUAL_UINT32(10, tapTraceAt(trace, 0).t_ms); // the 10 oldest were overwritten
  TEST_ASSERT_EQUAL_UINT32(10, tapTraceSeqAt(trace, 0));
  TEST_ASSERT_EQUAL_UINT32(total - 1, tapTraceAt(trace, TAP_TRACE_CAPACITY - 1).t_ms);
  TEST_ASSERT_EQUAL_UINT32(total - 1, tapTraceSeqAt(trace, TAP_TRACE_CAPACITY - 1));
}

void test_phase_packing_saturates_and_keeps_not_reached() {
  const uint32_t packed = tapPackPhases(70000, TAP_PHASE_NONE);
  TEST_ASSERT_EQUAL_UINT16(TAP_PHASE_NONE - 1, tapPhaseHi(packed));
  TEST_ASSERT_EQUAL_UINT16(TAP_PHASE_NONE, tapPhaseLo(packed));
}

void test_cycle_line_shows_every_phase() {
  const TapRecord r = tapCycleRecord(263279492, 3, 0x020, TAP_CYCLE_MANUAL | TAP_CYCLE_FLASHED, 4100, 4300, 6600,
                                     8100, 8300, 1000, 1764073000, 7, 1);
  char line[256];
  tapTraceFormat(line, sizeof(line), 12, r);
  TEST_ASSERT_EQUAL_STRING("tap #12 cycle wake@263279492ms reason=3 ext1=0x020 wifi=+4100 fetch=+4300 dl=+6600 "
                           "draw=+8100 sleep=+8300 armed=1000ms epoch=1764073000 low_samples=7 task_presses=1 "
                           "manual flashed",
                           line);
}

void test_cycle_line_marks_phases_not_reached() {
  const TapRecord r =
    tapCycleRecord(5000, 4, 0, TAP_CYCLE_DEFERRED_IN, TAP_PHASE_NONE, TAP_PHASE_NONE, TAP_PHASE_NONE,
                   TAP_PHASE_NONE, 900, 1000, 0, 0, 0);
  char line[256];
  tapTraceFormat(line, sizeof(line), 0, r);
  TEST_ASSERT_NOT_NULL(strstr(line, "wifi=- fetch=- dl=- draw=- sleep=+900"));
  TEST_ASSERT_NOT_NULL(strstr(line, " deferred-in"));
}

void test_new_sources_phases_and_wifi_flags() {
  char line[256];
  tapTraceFormat(line, sizeof(line), 1, tapPressRecord(10, TapSource::BootGrace, TapPhase::BeforeDraw, 0x4, 0));
  TEST_ASSERT_NOT_NULL(strstr(line, "src=boot-grace phase=before-draw"));
  tapTraceFormat(line, sizeof(line), 2, tapManualRecord(10, TapSource::Retry, 3, 0, 4000, false));
  TEST_ASSERT_NOT_NULL(strstr(line, "src=retry"));
  tapTraceFormat(line, sizeof(line), 3,
                 tapCycleRecord(0, 3, 0x20, TAP_CYCLE_FAST_REJOIN | TAP_CYCLE_LEASE, 1, 2, 3, 4, 5, 6, 7, 0, 0));
  TEST_ASSERT_NOT_NULL(strstr(line, " fast-rejoin lease"));
}

void test_draw_fail_line_and_flag() {
  char line[256];
  tapTraceFormat(line, sizeof(line), 7, tapDrawFailRecord(5000, TapDrawFail::NoDecoder, 9100, 61234, 31744));
  TEST_ASSERT_EQUAL_STRING(
    "tap #7 draw-fail @5000ms reason=no-decoder-memory wake+9100ms free_internal=61234 largest_block=31744", line);
  tapTraceFormat(line, sizeof(line), 8, tapCycleRecord(0, 3, 0, TAP_CYCLE_DRAW_FAILED, 1, 2, 3, 4, 5, 6, 7, 0, 0));
  TEST_ASSERT_NOT_NULL(strstr(line, " draw-failed"));
}

void test_press_and_manual_lines() {
  char line[256];
  tapTraceFormat(line, sizeof(line), 3, tapPressRecord(1000, TapSource::SleepSnapshot, TapPhase::SleepEntry, 0x200, 9100));
  TEST_ASSERT_EQUAL_STRING("tap #3 press @1000ms src=sleep-snapshot phase=sleep-entry keys=0x200 wake+9100ms", line);

  tapTraceFormat(line, sizeof(line), 4, tapManualRecord(1000, TapSource::DeferredWake, 3, 6900, 10400, false));
  TEST_ASSERT_EQUAL_STRING("tap #4 manual press@1000ms src=deferred-wake press->fetch=6900ms press->flash=10400ms "
                           "result=3",
                           line);

  tapTraceFormat(line, sizeof(line), 5, tapManualRecord(2000, TapSource::Task, 5, 15000, UINT32_MAX, true));
  TEST_ASSERT_EQUAL_STRING("tap #5 manual press@2000ms src=task press->fetch=15000ms press->flash=never result=5 "
                           "second-fetch",
                           line);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_garbage_memory_is_not_a_valid_ring);
  RUN_TEST(test_push_on_garbage_starts_a_clean_ring);
  RUN_TEST(test_ring_keeps_the_newest_records_in_order);
  RUN_TEST(test_phase_packing_saturates_and_keeps_not_reached);
  RUN_TEST(test_cycle_line_shows_every_phase);
  RUN_TEST(test_cycle_line_marks_phases_not_reached);
  RUN_TEST(test_press_and_manual_lines);
  RUN_TEST(test_new_sources_phases_and_wifi_flags);
  RUN_TEST(test_draw_fail_line_and_flag);
  return UNITY_END();
}
