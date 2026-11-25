#include <key_press_latch.h>
#include <unity.h>

static void feed(KeyPressLatch &latch, bool level_low, int samples) {
  for (int i = 0; i < samples; i++) keyPressLatchSample(latch, level_low);
}

void test_a_clean_press_latches_once() {
  KeyPressLatch latch;
  keyPressLatchInit(latch, false);

  TEST_ASSERT_FALSE(keyPressLatchSample(latch, true));
  TEST_ASSERT_TRUE(keyPressLatchSample(latch, true)); // debounced on the 2nd low sample
  feed(latch, true, 20); // still held: no second press
  feed(latch, false, 5);

  TEST_ASSERT_TRUE(keyPressLatchTake(latch));
  TEST_ASSERT_FALSE(keyPressLatchTake(latch)); // taking clears it
}

void test_a_single_low_glitch_is_not_a_press() {
  KeyPressLatch latch;
  keyPressLatchInit(latch, false);

  keyPressLatchSample(latch, true);
  keyPressLatchSample(latch, false);
  keyPressLatchSample(latch, true);
  keyPressLatchSample(latch, false);

  TEST_ASSERT_FALSE(keyPressLatchTake(latch));
}

void test_release_bounce_is_not_a_second_press() {
  KeyPressLatch latch;
  keyPressLatchInit(latch, false);
  feed(latch, true, 5);
  TEST_ASSERT_TRUE(keyPressLatchTake(latch));

  // Let go with a bouncy contact: high, low, high, low, then settled high.
  keyPressLatchSample(latch, false);
  keyPressLatchSample(latch, true);
  keyPressLatchSample(latch, false);
  keyPressLatchSample(latch, true);
  feed(latch, false, 5);

  TEST_ASSERT_FALSE(keyPressLatchTake(latch));
}

void test_the_key_that_woke_the_device_is_not_a_press() {
  // Sampling starts while the wake press is still held down.
  KeyPressLatch latch;
  keyPressLatchInit(latch, true);
  feed(latch, true, 10);
  feed(latch, false, 5);

  TEST_ASSERT_FALSE(keyPressLatchTake(latch));

  // A fresh press after that is.
  feed(latch, true, 2);
  TEST_ASSERT_TRUE(keyPressLatchTake(latch));
}

void test_a_stuck_key_never_latches() {
  KeyPressLatch latch;
  keyPressLatchInit(latch, true);
  feed(latch, true, 1000);

  TEST_ASSERT_FALSE(keyPressLatchTake(latch));
  TEST_ASSERT_FALSE(keyPressLatchIsStarting(latch, true));
}

void test_a_press_just_starting_is_seen_before_debounce() {
  KeyPressLatch latch;
  keyPressLatchInit(latch, false);
  feed(latch, false, 3);

  // First low sample has not debounced yet, but sleep cannot wait for it.
  keyPressLatchSample(latch, true);
  TEST_ASSERT_FALSE(keyPressLatchTake(latch));
  TEST_ASSERT_TRUE(keyPressLatchIsStarting(latch, true));
  TEST_ASSERT_FALSE(keyPressLatchIsStarting(latch, false));
}

void test_two_presses_while_awake_latch_one_pending_refresh() {
  KeyPressLatch latch;
  keyPressLatchInit(latch, false);
  feed(latch, true, 3);
  feed(latch, false, 3);
  feed(latch, true, 3);

  TEST_ASSERT_TRUE(keyPressLatchTake(latch));
  TEST_ASSERT_FALSE(keyPressLatchTake(latch));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_a_clean_press_latches_once);
  RUN_TEST(test_a_single_low_glitch_is_not_a_press);
  RUN_TEST(test_release_bounce_is_not_a_second_press);
  RUN_TEST(test_the_key_that_woke_the_device_is_not_a_press);
  RUN_TEST(test_a_stuck_key_never_latches);
  RUN_TEST(test_a_press_just_starting_is_seen_before_debounce);
  RUN_TEST(test_two_presses_while_awake_latch_one_pending_refresh);
  return UNITY_END();
}
