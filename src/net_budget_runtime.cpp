#include <Arduino.h>
#include <net_budget.h>
#include <net_budget_runtime.h>

#include "esp_log.h"

namespace {

// One budget per wake. Regular RAM is cleared by deep sleep, so this starts
// empty on every cycle, which is exactly what a per-wake deadline wants.
NetBudget budget = {};

uint64_t nowMs() { return (uint64_t)millis(); }

} // namespace

void netBudgetBegin(void) {
  const uint64_t now = nowMs();
  if (netBudgetIsStarted(budget)) return;
  netBudgetStart(budget, now, NET_BUDGET_LIMIT_MS);
  // Visible on the console regardless of the verbose-logging setting, like the
  // fetch timing lines, so a capture shows the allowance in force.
  esp_log_write(ESP_LOG_INFO, "net-budget", "net budget: %u ms for this wake's network work (at %llu ms)\n",
                (unsigned)NET_BUDGET_LIMIT_MS, (unsigned long long)now);
}

uint32_t netBudgetRemainingMs(void) {
  netBudgetBegin();
  return netBudgetRemainingMs(budget, nowMs());
}

uint64_t netBudgetElapsedMs(void) {
  netBudgetBegin();
  return netBudgetElapsedMs(budget, nowMs());
}

bool netBudgetExpired(void) {
  netBudgetBegin();
  return netBudgetExpired(budget, nowMs());
}

uint32_t netBudgetClampTimeoutMs(uint32_t requested_ms) {
  netBudgetBegin();
  return netBudgetClampTimeoutMs(budget, nowMs(), requested_ms);
}

bool netBudgetTakeReconnect(void) {
  netBudgetBegin();
  return netBudgetTakeReconnect(budget, nowMs());
}

void netBudgetMarkFetchAborted(void) { netBudgetMarkFetchAborted(budget); }

bool netBudgetFetchAborted(void) { return budget.fetch_aborted; }
