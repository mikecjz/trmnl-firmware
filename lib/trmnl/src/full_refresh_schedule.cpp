#include <full_refresh_schedule.h>
#include <sleep_duration.h>

uint32_t fullRefreshIntervalSeconds(uint32_t server_value) {
  if (server_value == 0) return FULL_REFRESH_INTERVAL_DEFAULT_S;
  if (server_value > FULL_REFRESH_INTERVAL_MAX_S) return FULL_REFRESH_INTERVAL_MAX_S;
  return server_value;
}

uint32_t fullRefreshAgeSeconds(const FullRefreshState &state, uint32_t now_epoch) {
  if (now_epoch != 0 && state.last_full_epoch != 0) {
    // sleepDurationSeconds() is step-back safe (returns 0 if the clock moved
    // backwards). Fall through to the accumulated sleep time in that case: a
    // bad clock should make us flash early, never suppress the flush for hours.
    const uint32_t epoch_age = sleepDurationSeconds(now_epoch, state.last_full_epoch);
    return epoch_age > state.age_seconds ? epoch_age : state.age_seconds;
  }
  return state.age_seconds;
}

bool fullRefreshIsDue(const FullRefreshState &state, uint32_t now_epoch, uint32_t server_value) {
  if (state.partials_since_full >= FULL_REFRESH_MAX_CONSECUTIVE_PARTIALS) return true;
  return fullRefreshAgeSeconds(state, now_epoch) >= fullRefreshIntervalSeconds(server_value);
}

void fullRefreshNoteSleep(FullRefreshState &state, uint32_t sleep_seconds) {
  const uint32_t room = UINT32_MAX - state.age_seconds;
  state.age_seconds += sleep_seconds > room ? room : sleep_seconds;
}

void fullRefreshNoteFlash(FullRefreshState &state, uint32_t now_epoch) {
  state.last_full_epoch = now_epoch;
  state.age_seconds = 0;
  state.partials_since_full = 0;
}

void fullRefreshNotePartial(FullRefreshState &state) {
  if (state.partials_since_full != UINT32_MAX) state.partials_since_full++;
}
