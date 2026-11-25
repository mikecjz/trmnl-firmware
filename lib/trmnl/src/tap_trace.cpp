#include <stdio.h>
#include <string.h>
#include <tap_trace.h>

namespace {
  uint16_t sat16(uint32_t ms) { return ms >= TAP_PHASE_NONE ? (uint16_t)(TAP_PHASE_NONE - 1) : (uint16_t)ms; }

  const char *sourceName(uint8_t source) {
    switch ((TapSource)source) {
    case TapSource::Task:
      return "task";
    case TapSource::SleepSnapshot:
      return "sleep-snapshot";
    case TapSource::KeyWake:
      return "key-wake";
    case TapSource::DeferredWake:
      return "deferred-wake";
    case TapSource::Retry:
      return "retry";
    case TapSource::BootGrace:
      return "boot-grace";
    default:
      return "unknown";
    }
  }

  const char *phaseName(uint16_t phase) {
    switch ((TapPhase)phase) {
    case TapPhase::BeforeFetch:
      return "before-fetch";
    case TapPhase::AfterFetch:
      return "after-fetch";
    case TapPhase::SleepEntry:
      return "sleep-entry";
    case TapPhase::Wake:
      return "wake";
    case TapPhase::BeforeDraw:
      return "before-draw";
    default:
      return "?";
    }
  }

  // "+1234" or "-" for a phase that was not reached.
  void phaseStr(char *out, size_t size, uint16_t ms) {
    if (ms == TAP_PHASE_NONE)
      snprintf(out, size, "-");
    else
      snprintf(out, size, "+%u", (unsigned)ms);
  }

  void delayStr(char *out, size_t size, uint32_t ms) {
    if (ms == UINT32_MAX)
      snprintf(out, size, "never");
    else
      snprintf(out, size, "%lums", (unsigned long)ms);
  }
} // namespace

uint32_t tapPackPhases(uint32_t hi_ms, uint32_t lo_ms) {
  const uint16_t hi = hi_ms == TAP_PHASE_NONE ? TAP_PHASE_NONE : sat16(hi_ms);
  const uint16_t lo = lo_ms == TAP_PHASE_NONE ? TAP_PHASE_NONE : sat16(lo_ms);
  return ((uint32_t)hi << 16) | lo;
}

uint16_t tapPhaseHi(uint32_t packed) { return (uint16_t)(packed >> 16); }

uint16_t tapPhaseLo(uint32_t packed) { return (uint16_t)(packed & 0xFFFF); }

bool tapTraceIsValid(const TapTrace &trace) {
  return trace.magic == TAP_TRACE_MAGIC && trace.head < TAP_TRACE_CAPACITY && trace.count <= TAP_TRACE_CAPACITY;
}

void tapTraceReset(TapTrace &trace) {
  memset(&trace, 0, sizeof(trace));
  trace.magic = TAP_TRACE_MAGIC;
}

void tapTracePush(TapTrace &trace, const TapRecord &record) {
  if (!tapTraceIsValid(trace)) tapTraceReset(trace);
  trace.records[trace.head] = record;
  trace.head = (uint16_t)((trace.head + 1) % TAP_TRACE_CAPACITY);
  if (trace.count < TAP_TRACE_CAPACITY) trace.count++;
  trace.seq++;
}

const TapRecord &tapTraceAt(const TapTrace &trace, uint16_t i) {
  const uint16_t oldest = (uint16_t)((trace.head + TAP_TRACE_CAPACITY - trace.count) % TAP_TRACE_CAPACITY);
  return trace.records[(oldest + i) % TAP_TRACE_CAPACITY];
}

uint32_t tapTraceSeqAt(const TapTrace &trace, uint16_t i) { return trace.seq - trace.count + i; }

TapRecord tapCycleRecord(uint32_t wake_rtc_ms, uint16_t wake_reason, uint16_t ext1_mask, uint8_t flags,
                         uint32_t wifi_up_ms, uint32_t fetch_start_ms, uint32_t download_ms, uint32_t draw_ms,
                         uint32_t sleep_at_ms, uint32_t armed_sleep_ms, uint32_t epoch_at_sleep,
                         uint16_t low_samples, uint16_t task_presses) {
  // Layout:
  //   a     flags (TAP_CYCLE_*)
  //   b     wake reason (low byte) | task presses (high byte, saturated)
  //   v[0]  wifi up | fetch start          (ms since wake, TAP_PHASE_NONE = not reached)
  //   v[1]  download done | draw done      (ms since wake)
  //   v[2]  sleep entry (ms since wake, high 16) | ext1 wake mask (low 16)
  //   v[3]  armed sleep ms (low 24) | low key samples this wake (high 8, saturated)
  //   v[4]  wall-clock epoch at sleep entry (0 = clock not set)
  TapRecord r = {};
  r.kind = (uint8_t)TapRecordKind::Cycle;
  r.a = flags;
  r.b = (uint16_t)((wake_reason & 0xFF) | ((uint16_t)(task_presses > 0xFF ? 0xFF : task_presses) << 8));
  r.t_ms = wake_rtc_ms;
  r.v[0] = tapPackPhases(wifi_up_ms, fetch_start_ms);
  r.v[1] = tapPackPhases(download_ms, draw_ms);
  r.v[2] = ((uint32_t)sat16(sleep_at_ms) << 16) | ext1_mask;
  r.v[3] = (armed_sleep_ms > 0x00FFFFFF ? 0x00FFFFFF : armed_sleep_ms) |
           ((uint32_t)(low_samples > 0xFF ? 0xFF : low_samples) << 24);
  r.v[4] = epoch_at_sleep;
  return r;
}

TapRecord tapPressRecord(uint32_t press_rtc_ms, TapSource source, TapPhase phase, uint16_t key_mask,
                         uint32_t ms_since_wake) {
  TapRecord r = {};
  r.kind = (uint8_t)TapRecordKind::Press;
  r.a = (uint8_t)source;
  r.b = (uint16_t)phase;
  r.t_ms = press_rtc_ms;
  r.v[0] = key_mask;
  r.v[1] = ms_since_wake;
  return r;
}

TapRecord tapManualRecord(uint32_t press_rtc_ms, TapSource source, uint16_t request_result, uint32_t fetch_delay_ms,
                          uint32_t flash_delay_ms, bool second_fetch) {
  TapRecord r = {};
  r.kind = (uint8_t)TapRecordKind::Manual;
  r.a = (uint8_t)source;
  r.b = request_result;
  r.t_ms = press_rtc_ms;
  r.v[0] = fetch_delay_ms;
  r.v[1] = flash_delay_ms;
  r.v[2] = second_fetch ? 1 : 0;
  return r;
}

int tapTraceFormat(char *out, size_t size, uint32_t seq, const TapRecord &r) {
  switch ((TapRecordKind)r.kind) {
  case TapRecordKind::Cycle: {
    char wifi[8], fetch[8], dl[8], draw[8];
    phaseStr(wifi, sizeof(wifi), tapPhaseHi(r.v[0]));
    phaseStr(fetch, sizeof(fetch), tapPhaseLo(r.v[0]));
    phaseStr(dl, sizeof(dl), tapPhaseHi(r.v[1]));
    phaseStr(draw, sizeof(draw), tapPhaseLo(r.v[1]));
    return snprintf(out, size,
                    "tap #%lu cycle wake@%lums reason=%u ext1=0x%03x wifi=%s fetch=%s dl=%s draw=%s sleep=+%u "
                    "armed=%lums epoch=%lu low_samples=%u task_presses=%u%s%s%s%s%s%s",
                    (unsigned long)seq, (unsigned long)r.t_ms, (unsigned)(r.b & 0xFF), (unsigned)(r.v[2] & 0xFFFF),
                    wifi, fetch, dl, draw, (unsigned)(r.v[2] >> 16), (unsigned long)(r.v[3] & 0x00FFFFFF),
                    (unsigned long)r.v[4], (unsigned)(r.v[3] >> 24), (unsigned)(r.b >> 8),
                    (r.a & TAP_CYCLE_DEFERRED_IN) ? " deferred-in" : "",
                    (r.a & TAP_CYCLE_DEFERRED_OUT) ? " deferred-out" : "", (r.a & TAP_CYCLE_MANUAL) ? " manual" : "",
                    (r.a & TAP_CYCLE_FLASHED) ? " flashed" : "", (r.a & TAP_CYCLE_FAST_REJOIN) ? " fast-rejoin" : "",
                    (r.a & TAP_CYCLE_LEASE) ? " lease" : "");
  }
  case TapRecordKind::Press:
    return snprintf(out, size, "tap #%lu press @%lums src=%s phase=%s keys=0x%03lx wake+%lums", (unsigned long)seq,
                    (unsigned long)r.t_ms, sourceName(r.a), phaseName(r.b), (unsigned long)r.v[0],
                    (unsigned long)r.v[1]);
  case TapRecordKind::Manual: {
    char fetch[16], flash[16];
    delayStr(fetch, sizeof(fetch), r.v[0]);
    delayStr(flash, sizeof(flash), r.v[1]);
    return snprintf(out, size, "tap #%lu manual press@%lums src=%s press->fetch=%s press->flash=%s result=%u%s",
                    (unsigned long)seq, (unsigned long)r.t_ms, sourceName(r.a), fetch, flash, (unsigned)r.b,
                    r.v[2] ? " second-fetch" : "");
  }
  default:
    return snprintf(out, size, "tap #%lu ?", (unsigned long)seq);
  }
}
