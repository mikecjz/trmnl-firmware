#include <net_budget.h>

namespace {

uint64_t deadlineMs(const NetBudget &budget) { return budget.started_ms + (uint64_t)budget.limit_ms; }

} // namespace

void netBudgetStart(NetBudget &budget, uint64_t now_ms, uint32_t limit_ms) {
  if (budget.started) return; // the first attempt of the cycle owns the anchor
  budget.started_ms = now_ms;
  budget.limit_ms = limit_ms;
  budget.started = true;
}

bool netBudgetIsStarted(const NetBudget &budget) { return budget.started; }

uint32_t netBudgetRemainingMs(const NetBudget &budget, uint64_t now_ms) {
  if (!budget.started) return UINT32_MAX; // no deadline set yet: nothing to run out of
  const uint64_t deadline = deadlineMs(budget);
  if (now_ms >= deadline) return 0;
  const uint64_t remaining = deadline - now_ms;
  return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}

uint64_t netBudgetElapsedMs(const NetBudget &budget, uint64_t now_ms) {
  if (!budget.started || now_ms < budget.started_ms) return 0;
  return now_ms - budget.started_ms;
}

bool netBudgetExpired(const NetBudget &budget, uint64_t now_ms) {
  return budget.started && now_ms >= deadlineMs(budget);
}

uint32_t netBudgetClampTimeoutMs(const NetBudget &budget, uint64_t now_ms, uint32_t requested_ms) {
  const uint32_t remaining = netBudgetRemainingMs(budget, now_ms);
  return remaining < requested_ms ? remaining : requested_ms;
}

bool netBudgetTakeReconnect(NetBudget &budget, uint64_t now_ms) {
  if (budget.reconnect_used) return false;
  if (netBudgetRemainingMs(budget, now_ms) < NET_BUDGET_MIN_RECONNECT_MS) return false;
  budget.reconnect_used = true;
  return true;
}

void netBudgetMarkFetchAborted(NetBudget &budget) { budget.fetch_aborted = true; }
