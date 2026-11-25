#pragma once

#include <stdint.h>

/**
 * One deadline for all the network work a single wake does.
 *
 * The request, the body download, the retry ladder and the Wi-Fi reconnect
 * ladder each have their own timeout, but nothing used to add them up: a Wi-Fi
 * hiccup could therefore enter a multi-minute reconnect ladder before *every*
 * retry attempt and hold the device awake for most of its awake window. This
 * budget is shared by everything the fetch cycle does, so the wake ends on one
 * deadline instead of on the sum of the layers' local limits.
 *
 * Times are milliseconds of a clock that only has to be monotonic within one
 * boot (millis() on the device): deep sleep restarts it, and so does the
 * budget, because every wake is a new cycle.
 *
 * Plain aggregate on purpose, so it needs no initialization beyond {}.
 */
struct NetBudget {
  /// Time of the first network operation of the wake, when the clock started.
  uint64_t started_ms;
  /// The whole allowance, in ms; the deadline is started_ms + limit_ms.
  uint32_t limit_ms;
  bool started;
  /// The single reconnect this wake is allowed to try.
  bool reconnect_used;
  /// A request gave up because the budget ran out; the wake should sleep on its
  /// schedule instead of retrying or drawing an error.
  bool fetch_aborted;
};

/// Whole allowance for one wake's network work, counted from the first attempt
/// of the fetch cycle. 90 s covers a normal boot connect + fetch with room to
/// spare, while keeping a stalled network well inside the awake window.
static constexpr uint32_t NET_BUDGET_LIMIT_MS = 90000;

/// Do not start an HTTP attempt with less than this left: it cannot finish, and
/// the connect/read timeouts would be cut to an amount no server can answer in.
static constexpr uint32_t NET_BUDGET_MIN_ATTEMPT_MS = 2000;

/// Do not start the Wi-Fi reconnect ladder (a 10 s reconnect wait followed by a
/// fresh join) with less than this left.
static constexpr uint32_t NET_BUDGET_MIN_RECONNECT_MS = 10000;

/// Start the budget at `now_ms`. Only the first call fixes the anchor, so a
/// later call in the same wake does not extend the deadline.
void netBudgetStart(NetBudget &budget, uint64_t now_ms, uint32_t limit_ms);

/// True once the first call above has anchored the budget.
bool netBudgetIsStarted(const NetBudget &budget);

/// Milliseconds left before the deadline; 0 once it has passed. Before the
/// budget is started there is no deadline yet and this returns UINT32_MAX, so a
/// missed netBudgetStart() cannot abort network work on its own.
uint32_t netBudgetRemainingMs(const NetBudget &budget, uint64_t now_ms);

/// Milliseconds between the anchor and `now_ms`; 0 when not started.
uint64_t netBudgetElapsedMs(const NetBudget &budget, uint64_t now_ms);

/// True when the budget has started and its deadline is at or before `now_ms`.
bool netBudgetExpired(const NetBudget &budget, uint64_t now_ms);

/// `requested_ms` cut down to what is left, so one attempt cannot outlive the
/// budget through its own timeout.
uint32_t netBudgetClampTimeoutMs(const NetBudget &budget, uint64_t now_ms, uint32_t requested_ms);

/// At most one reconnect per wake, and only when there is enough budget left to
/// make it worthwhile. The first successful call consumes it.
bool netBudgetTakeReconnect(NetBudget &budget, uint64_t now_ms);

/// Record that a request was abandoned because the budget ran out.
void netBudgetMarkFetchAborted(NetBudget &budget);
