#pragma once

#include <stdint.h>

/**
 * Debounced "was this key pressed?" latch for active-low keys that are sampled
 * at a fixed period while the device is awake.
 *
 * A key only counts as pressed after it has been seen released and then reads
 * low for KEY_PRESS_LATCH_STABLE_SAMPLES samples in a row. That rejects:
 *   - contact bounce, including the bounce when a key is let go;
 *   - a key that is already down when sampling starts (the press that woke the
 *     device, or a key held by a stand / stuck), which never latches until it
 *     has been released first.
 *
 * Plain aggregate with no dependencies, so it is unit-tested natively.
 */

/// Samples a level must hold before the debounced state follows it.
static constexpr uint8_t KEY_PRESS_LATCH_STABLE_SAMPLES = 2;

struct KeyPressLatch {
  /// Debounced state: true while the key is held down.
  bool pressed;
  /// Consecutive samples that disagree with `pressed`.
  uint8_t run;
  /// A new press was seen and not yet taken.
  bool latched;
  /// Samples left in which releasing a key that was already down when sampling
  /// started still counts as a press (see keyPressLatchInitWithGrace()).
  uint16_t grace;
};

/// Starts tracking from the key's current level. A key that is already down
/// (low) does not count as a press.
void keyPressLatchInit(KeyPressLatch &latch, bool level_low);

/// Like keyPressLatchInit(), for a key that may have been tapped just before
/// sampling could start (the boot window). If the key is down now and is let go
/// within `grace_samples`, that counts as a press. A key that stays down longer
/// (stuck, or held by a stand) still never latches.
void keyPressLatchInitWithGrace(KeyPressLatch &latch, bool level_low, uint16_t grace_samples);

/// Feeds one sample. Returns true when this sample completed a new press.
bool keyPressLatchSample(KeyPressLatch &latch, bool level_low);

/// Returns and clears the latched press.
bool keyPressLatchTake(KeyPressLatch &latch);

/// A press that has started but is not yet debounced: the key was released
/// and now reads low. Used just before deep sleep, when there is no time left
/// to wait for the debounce.
bool keyPressLatchIsStarting(const KeyPressLatch &latch, bool level_low);
