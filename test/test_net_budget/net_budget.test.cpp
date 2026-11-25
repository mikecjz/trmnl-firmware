#include <net_budget.h>
#include <stdio.h>
#include <unity.h>

static const uint64_t S = 1000ULL; // the budget is in milliseconds
static const uint32_t LIMIT = 90000; // 90 s, the shipped allowance

static NetBudget startedAt(uint64_t now) {
  NetBudget budget = {};
  netBudgetStart(budget, now, LIMIT);
  return budget;
}

// --- The anchor ------------------------------------------------------------

void test_first_call_anchors_the_budget() {
  NetBudget budget = {};
  TEST_ASSERT_FALSE(netBudgetIsStarted(budget));

  netBudgetStart(budget, 5000, LIMIT);
  TEST_ASSERT_TRUE(netBudgetIsStarted(budget));
  TEST_ASSERT_EQUAL_UINT64(5000, budget.started_ms);
  TEST_ASSERT_EQUAL_UINT32(LIMIT, budget.limit_ms);
}

void test_a_later_start_does_not_extend_the_deadline() {
  NetBudget budget = startedAt(5000);

  // A second call from the image fetch must not restart the clock.
  netBudgetStart(budget, 70000, LIMIT);
  TEST_ASSERT_EQUAL_UINT64(5000, budget.started_ms);
  TEST_ASSERT_EQUAL_UINT32(60000, netBudgetRemainingMs(budget, 35000));
}

void test_not_started_spends_nothing() {
  NetBudget budget = {};

  // No deadline yet: a missed begin cannot abort network work on its own.
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, netBudgetRemainingMs(budget, 123456));
  TEST_ASSERT_FALSE(netBudgetExpired(budget, 123456));

  // But the first request that does start it gets the full allowance.
  netBudgetStart(budget, 123456, LIMIT);
  TEST_ASSERT_EQUAL_UINT32(LIMIT, netBudgetRemainingMs(budget, 123456));
}

// --- Spending -------------------------------------------------------------

void test_remaining_counts_down_from_the_anchor() {
  const NetBudget budget = startedAt(100 * S);

  TEST_ASSERT_EQUAL_UINT32(LIMIT, netBudgetRemainingMs(budget, 100 * S));
  TEST_ASSERT_EQUAL_UINT32((uint32_t)(LIMIT - 30 * S), netBudgetRemainingMs(budget, 130 * S));
  TEST_ASSERT_EQUAL_UINT32(1, netBudgetRemainingMs(budget, 100 * S + LIMIT - 1));
}

void test_expired_at_the_deadline_and_stays_there() {
  const NetBudget budget = startedAt(100 * S);

  TEST_ASSERT_FALSE(netBudgetExpired(budget, 100 * S + LIMIT - 1));
  TEST_ASSERT_TRUE(netBudgetExpired(budget, 100 * S + LIMIT));
  TEST_ASSERT_TRUE(netBudgetExpired(budget, 100 * S + 10 * LIMIT));
  TEST_ASSERT_EQUAL_UINT32(0, netBudgetRemainingMs(budget, 100 * S + LIMIT));
  TEST_ASSERT_EQUAL_UINT32(0, netBudgetRemainingMs(budget, 100 * S + 10 * LIMIT));
}

void test_elapsed_reports_the_time_spent() {
  const NetBudget budget = startedAt(100 * S);

  TEST_ASSERT_EQUAL_UINT64(0, netBudgetElapsedMs(budget, 100 * S));
  TEST_ASSERT_EQUAL_UINT64(45000, netBudgetElapsedMs(budget, 145 * S));
  // A clock that went backwards (it does not on the device) must not underflow.
  TEST_ASSERT_EQUAL_UINT64(0, netBudgetElapsedMs(budget, 99 * S));
}

// --- What one attempt may ask for -----------------------------------------

void test_a_timeout_is_cut_to_what_is_left() {
  const NetBudget budget = startedAt(100 * S);

  // 15 s connect timeout with 50 s left: unchanged.
  TEST_ASSERT_EQUAL_UINT32(15000, netBudgetClampTimeoutMs(budget, 100 * S, 15000));
  // 15 s request with 3 s left: cut to 3 s.
  TEST_ASSERT_EQUAL_UINT32(3000, netBudgetClampTimeoutMs(budget, 100 * S + 87 * S, 15000));
  // Spent: nothing left to give an attempt.
  TEST_ASSERT_EQUAL_UINT32(0, netBudgetClampTimeoutMs(budget, 100 * S + LIMIT, 15000));
}

void test_an_attempt_below_the_minimum_is_not_worth_starting() {
  const NetBudget budget = startedAt(0);

  // The retry loop's rule: room for the backoff plus a usable attempt, or stop.
  const uint32_t needed = 2000 + NET_BUDGET_MIN_ATTEMPT_MS;
  TEST_ASSERT_TRUE(netBudgetRemainingMs(budget, LIMIT - needed - 1) > needed);
  TEST_ASSERT_TRUE(netBudgetRemainingMs(budget, LIMIT - needed) <= needed);
}

// --- One reconnect per wake -----------------------------------------------

void test_reconnect_is_allowed_once() {
  NetBudget budget = startedAt(0);

  TEST_ASSERT_TRUE(netBudgetTakeReconnect(budget, 5 * S));
  TEST_ASSERT_TRUE(budget.reconnect_used);
  // Every later attempt in the same wake has to fail fast instead.
  TEST_ASSERT_FALSE(netBudgetTakeReconnect(budget, 10 * S));
  TEST_ASSERT_FALSE(netBudgetTakeReconnect(budget, 20 * S));
}

void test_reconnect_is_not_started_with_too_little_budget() {
  NetBudget budget = startedAt(0);

  // 1 s left: the ladder would be cut off before it could finish, so do not
  // enter it (and do not spend the single reconnect).
  TEST_ASSERT_FALSE(netBudgetTakeReconnect(budget, LIMIT - 1 * S));
  TEST_ASSERT_FALSE(budget.reconnect_used);

  // Enough left: allowed.
  TEST_ASSERT_TRUE(netBudgetTakeReconnect(budget, LIMIT - NET_BUDGET_MIN_RECONNECT_MS));
}

void test_reconnect_is_never_allowed_once_the_budget_is_spent() {
  NetBudget budget = startedAt(0);

  TEST_ASSERT_FALSE(netBudgetTakeReconnect(budget, LIMIT));
  TEST_ASSERT_FALSE(budget.reconnect_used);
}

// --- The abort signal -----------------------------------------------------

void test_an_abort_is_recorded_for_the_request_policy() {
  NetBudget budget = startedAt(0);
  TEST_ASSERT_FALSE(budget.fetch_aborted);

  netBudgetMarkFetchAborted(budget);
  TEST_ASSERT_TRUE(budget.fetch_aborted);

  // Recording it twice is harmless.
  netBudgetMarkFetchAborted(budget);
  TEST_ASSERT_TRUE(budget.fetch_aborted);
}

// --- One budget across the requests of a cycle ----------------------------

void test_api_and_image_share_the_one_budget() {
  // A cycle starts at t=10 s: the API request spends 30 s, then the image
  // request must see only the rest of the allowance, and the next attempt
  // after a spent budget is abandoned rather than given a fresh 90 s.
  NetBudget budget = startedAt(10 * S);

  TEST_ASSERT_FALSE(netBudgetExpired(budget, 40 * S));
  TEST_ASSERT_EQUAL_UINT32(60000, netBudgetRemainingMs(budget, 40 * S));

  // The image attempt clamps its 30 s server timeout to the 60 s still left.
  TEST_ASSERT_EQUAL_UINT32(30000, netBudgetClampTimeoutMs(budget, 40 * S, 30000));

  // At t=10 s + 90 s the wake is done: no further attempt.
  TEST_ASSERT_TRUE(netBudgetExpired(budget, 10 * S + LIMIT));
  TEST_ASSERT_EQUAL_UINT32(0, netBudgetRemainingMs(budget, 10 * S + LIMIT));
}

void test_the_shipped_allowance_is_in_the_documented_range() {
  // The issue asked for "start around 60-90 s"; keep the constant honest.
  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(60000, NET_BUDGET_LIMIT_MS);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(90000, NET_BUDGET_LIMIT_MS);
  // And much shorter than the awake-window abort it is meant to pre-empt.
  TEST_ASSERT_TRUE(NET_BUDGET_LIMIT_MS < 20UL * 60UL * 1000UL);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_first_call_anchors_the_budget);
  RUN_TEST(test_a_later_start_does_not_extend_the_deadline);
  RUN_TEST(test_not_started_spends_nothing);
  RUN_TEST(test_remaining_counts_down_from_the_anchor);
  RUN_TEST(test_expired_at_the_deadline_and_stays_there);
  RUN_TEST(test_elapsed_reports_the_time_spent);
  RUN_TEST(test_a_timeout_is_cut_to_what_is_left);
  RUN_TEST(test_an_attempt_below_the_minimum_is_not_worth_starting);
  RUN_TEST(test_reconnect_is_allowed_once);
  RUN_TEST(test_reconnect_is_not_started_with_too_little_budget);
  RUN_TEST(test_reconnect_is_never_allowed_once_the_budget_is_spent);
  RUN_TEST(test_an_abort_is_recorded_for_the_request_policy);
  RUN_TEST(test_api_and_image_share_the_one_budget);
  RUN_TEST(test_the_shipped_allowance_is_in_the_documented_range);
  return UNITY_END();
}
