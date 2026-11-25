#include <key_press_latch.h>

void keyPressLatchInit(KeyPressLatch &latch, bool level_low) {
  latch.pressed = level_low;
  latch.run = 0;
  latch.latched = false;
}

bool keyPressLatchSample(KeyPressLatch &latch, bool level_low) {
  if (level_low == latch.pressed) {
    latch.run = 0;
    return false;
  }
  if (++latch.run < KEY_PRESS_LATCH_STABLE_SAMPLES) return false;

  latch.run = 0;
  latch.pressed = level_low;
  if (!level_low) return false; // a release
  latch.latched = true;
  return true;
}

bool keyPressLatchTake(KeyPressLatch &latch) {
  const bool latched = latch.latched;
  latch.latched = false;
  return latched;
}

bool keyPressLatchIsStarting(const KeyPressLatch &latch, bool level_low) { return level_low && !latch.pressed; }
