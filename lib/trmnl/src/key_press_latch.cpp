#include <key_press_latch.h>

void keyPressLatchInit(KeyPressLatch &latch, bool level_low) {
  latch.pressed = level_low;
  latch.run = 0;
  latch.latched = false;
  latch.grace = 0;
}

void keyPressLatchInitWithGrace(KeyPressLatch &latch, bool level_low, uint16_t grace_samples) {
  keyPressLatchInit(latch, level_low);
  latch.grace = level_low ? grace_samples : 0;
}

bool keyPressLatchSample(KeyPressLatch &latch, bool level_low) {
  const bool in_grace = latch.grace > 0;
  if (in_grace) latch.grace--;
  if (level_low == latch.pressed) {
    latch.run = 0;
    return false;
  }
  if (++latch.run < KEY_PRESS_LATCH_STABLE_SAMPLES) return false;

  latch.run = 0;
  latch.pressed = level_low;
  latch.grace = 0; // only the first release can count
  if (!level_low) {
    if (!in_grace) return false; // an ordinary release
    // A key that was already down when sampling started and came up quickly:
    // a tap that landed before sampling began.
    latch.latched = true;
    return true;
  }
  latch.latched = true;
  return true;
}

bool keyPressLatchTake(KeyPressLatch &latch) {
  const bool latched = latch.latched;
  latch.latched = false;
  return latched;
}

bool keyPressLatchIsStarting(const KeyPressLatch &latch, bool level_low) { return level_low && !latch.pressed; }
