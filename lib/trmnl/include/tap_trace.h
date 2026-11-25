#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * Refresh-key ("tap") trace: a small ring of fixed-size records that lives in
 * RTC no-init memory on the device, so it survives deep sleep and a
 * USB-console chip reset, and is printed on the next boot that is not a
 * deep-sleep wake (i.e. when someone opens the console).
 *
 * Times are milliseconds of the RTC clock (esp_rtc_get_time_us() / 1000), which
 * keeps counting through deep sleep. Every Cycle record carries the wall-clock
 * epoch and the RTC time it was taken at, so any RTC time in the ring can be
 * converted to wall-clock time and matched against when someone tapped.
 *
 * Plain aggregates and pure functions: unit-tested natively.
 */

static constexpr uint32_t TAP_TRACE_MAGIC = 0x54415054; // "TAPT"
static constexpr uint16_t TAP_TRACE_CAPACITY = 64;

enum class TapRecordKind : uint8_t {
  None = 0,
  /// One per wake, written at sleep entry: wake reason, phase timings, sleep.
  Cycle = 1,
  /// A refresh-key press, written when the firmware takes it.
  Press = 2,
  /// The outcome of the manual refresh a press asked for.
  Manual = 3,
  /// A draw that never reached the panel, with why and the internal heap then.
  DrawFail = 4,
};

/// Why a draw failed (TapRecord::a for DrawFail records).
enum class TapDrawFail : uint8_t {
  Unknown = 0,
  /// No memory for the PNG decoder (it needs one ~49 KB internal block).
  NoDecoder = 1,
  /// No memory for the 48 KB frame buffer.
  NoFrameBuffer = 2,
  /// The panel update itself failed.
  PanelUpdate = 3,
};

/// Who caught the press.
enum class TapSource : uint8_t {
  Unknown = 0,
  /// The awake-time sampling task (x4c_refresh_keys).
  Task = 1,
  /// The last check just before deep sleep; the press is deferred to a 100 ms re-wake.
  SleepSnapshot = 2,
  /// The key woke the chip from deep sleep (ext1).
  KeyWake = 3,
  /// The 100 ms re-wake after a SleepSnapshot press.
  DeferredWake = 4,
  /// A press whose manual fetch failed, retried by a 100 ms re-wake.
  Retry = 5,
  /// A key tapped in the boot window, before sampling started (seen on release).
  BootGrace = 6,
};

/// Where in the wake the press was taken.
enum class TapPhase : uint8_t {
  /// Before this wake's fetch: boot / Wi-Fi join. Merged into this wake's fetch.
  BeforeFetch = 0,
  /// During the fetch or draw: needs a second fetch.
  AfterFetch = 1,
  /// At sleep entry: deferred to the next wake.
  SleepEntry = 2,
  /// The press that woke the chip.
  Wake = 3,
  /// After the download, before the image is drawn: that draw becomes the flash.
  BeforeDraw = 4,
};

/// Cycle flags (TapRecord::a for Cycle records).
enum : uint8_t {
  TAP_CYCLE_DEFERRED_IN = 1 << 0,  // this wake served a deferred press
  TAP_CYCLE_DEFERRED_OUT = 1 << 1, // this wake deferred a press to the next one
  TAP_CYCLE_MANUAL = 1 << 2,       // this wake ran at least one manual refresh
  TAP_CYCLE_FLASHED = 1 << 3,      // this wake did a full flash
  TAP_CYCLE_FAST_REJOIN = 1 << 4,  // Wi-Fi rejoined the remembered AP without a scan
  TAP_CYCLE_LEASE = 1 << 5,        // ... and reused the remembered DHCP lease
  TAP_CYCLE_DRAW_FAILED = 1 << 6,  // a draw this wake never reached the panel
};

/// "Not reached in this wake" for the ms-since-wake phase fields.
static constexpr uint16_t TAP_PHASE_NONE = 0xFFFF;

struct TapRecord {
  uint8_t kind;  // TapRecordKind
  uint8_t a;     // Cycle: flags; Press: TapSource; Manual: TapSource
  uint16_t b;    // Cycle: wake reason; Press: TapPhase; Manual: request result
  uint32_t t_ms; // Cycle: wake start (RTC ms); Press/Manual: press time (RTC ms)
  uint32_t v[5]; // kind-specific, see tapTraceFormat()
};

struct TapTrace {
  uint32_t magic;
  uint16_t head;  // next slot to write
  uint16_t count; // valid records (<= capacity)
  uint32_t seq;   // sequence number of the next record
  TapRecord records[TAP_TRACE_CAPACITY];
};

/// Packs two ms-since-wake values (saturated at 0xFFFE; TAP_PHASE_NONE = not reached).
uint32_t tapPackPhases(uint32_t hi_ms, uint32_t lo_ms);
uint16_t tapPhaseHi(uint32_t packed);
uint16_t tapPhaseLo(uint32_t packed);

/// True when `trace` holds a ring written by this firmware (RTC no-init memory
/// is random after power-on).
bool tapTraceIsValid(const TapTrace &trace);

/// Empties the ring.
void tapTraceReset(TapTrace &trace);

/// Appends a record, overwriting the oldest when full. Resets an invalid ring first.
void tapTracePush(TapTrace &trace, const TapRecord &record);

/// The i-th record, oldest first (i < count).
const TapRecord &tapTraceAt(const TapTrace &trace, uint16_t i);

/// Sequence number of the i-th record, oldest first.
uint32_t tapTraceSeqAt(const TapTrace &trace, uint16_t i);

/// One human-readable line for a record (no trailing newline). Returns the
/// snprintf length.
int tapTraceFormat(char *out, size_t size, uint32_t seq, const TapRecord &record);

/// Builders, so the device code and the tests agree on the field layout.
TapRecord tapCycleRecord(uint32_t wake_rtc_ms, uint16_t wake_reason, uint16_t ext1_mask, uint8_t flags,
                         uint32_t wifi_up_ms, uint32_t fetch_start_ms, uint32_t download_ms, uint32_t draw_ms,
                         uint32_t sleep_at_ms, uint32_t armed_sleep_ms, uint32_t epoch_at_sleep,
                         uint16_t low_samples, uint16_t task_presses);
TapRecord tapPressRecord(uint32_t press_rtc_ms, TapSource source, TapPhase phase, uint16_t key_mask,
                         uint32_t ms_since_wake);
/// fetch_delay_ms / flash_delay_ms: press -> manual fetch start / press -> flash
/// done; UINT32_MAX when it did not happen.
TapRecord tapManualRecord(uint32_t press_rtc_ms, TapSource source, uint16_t request_result, uint32_t fetch_delay_ms,
                          uint32_t flash_delay_ms, bool second_fetch);
TapRecord tapDrawFailRecord(uint32_t rtc_ms, TapDrawFail reason, uint32_t ms_since_wake, uint32_t free_internal,
                            uint32_t largest_internal_block);
