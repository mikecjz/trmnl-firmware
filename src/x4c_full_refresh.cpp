#include <x4c_full_refresh.h>

#if defined(BOARD_XTEINK_X4_CLASSIC) && !defined(PARALLEL_EPD)

#include <esp_attr.h>
#include <full_refresh_schedule.h>
#include <misc/clock.h>

namespace {
  // RTC memory: survives deep sleep, cleared by a cold boot. That is exactly the
  // lifetime wanted here - a cold boot invalidates the panel baseline, so its
  // first paint is a full flash and re-bases this anyway.
  RTC_DATA_ATTR FullRefreshState s_state = {0, 0, 0};
} // namespace

bool x4c_full_refresh_due(uint32_t server_interval_seconds) {
  return fullRefreshIsDue(s_state, systemClock().getTime(), server_interval_seconds);
}

uint32_t x4c_full_refresh_age_seconds(void) { return fullRefreshAgeSeconds(s_state, systemClock().getTime()); }

void x4c_full_refresh_note_flash(void) { fullRefreshNoteFlash(s_state, systemClock().getTime()); }

void x4c_full_refresh_note_partial(void) { fullRefreshNotePartial(s_state); }

void x4c_full_refresh_note_sleep(uint32_t sleep_seconds) { fullRefreshNoteSleep(s_state, sleep_seconds); }

#endif // BOARD_XTEINK_X4_CLASSIC && !PARALLEL_EPD
