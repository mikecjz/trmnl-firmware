#pragma once

#include <stdint.h>

/**
 * Per-wake network deadline, the device side of lib/trmnl/.../net_budget.h.
 *
 * One budget is shared by every request the fetch cycle makes (API, image,
 * their retry ladders) and by the Wi-Fi reconnect that runs before an attempt.
 * It starts at the first network operation of the cycle; deep sleep resets the
 * clock the budget reads, so each wake gets a fresh one.
 */

/// Anchor the budget if it has not started yet. Call at the start of a fetch
/// cycle; every accessor below also calls it, so a missed begin cannot leave
/// the budget unset.
void netBudgetBegin(void);

/// Milliseconds left in this wake's network allowance; 0 once it is spent.
uint32_t netBudgetRemainingMs(void);

/// Milliseconds since the budget was anchored.
uint64_t netBudgetElapsedMs(void);

/// True once the allowance is spent: abandon the fetch instead of starting
/// another attempt.
bool netBudgetExpired(void);

/// `requested_ms` cut down to the allowance still left, so one HTTP attempt
/// cannot outlive the wake's budget through its own timeout.
uint32_t netBudgetClampTimeoutMs(uint32_t requested_ms);

/// Ask for the one Wi-Fi reconnect this wake may do. False once it has been
/// used, or when too little of the budget is left to start the ladder.
bool netBudgetTakeReconnect(void);

/// Record that a request was abandoned because the budget ran out. The request
/// policy reads this to sleep on the fetch schedule instead of retrying.
void netBudgetMarkFetchAborted(void);

/// True when a request in this wake gave up on the budget.
bool netBudgetFetchAborted(void);
