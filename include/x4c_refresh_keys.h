#pragma once

#ifdef BOARD_XTEINK_X4_CLASSIC
// Catches the X4 Classic's bottom (manual refresh) keys while the device is
// awake.
//
// Deep sleep only sees these keys through the ext1 wake mask, so a press is
// acted on only when it lands while the chip sleeps. At a short refresh
// interval most of each cycle is awake time (boot, Wi-Fi, fetch, draw), and a
// press then used to be dropped silently. This module samples the keys from a
// small task for the whole wake and keeps the press so the wake can honour it:
// re-run the fetch with a hard refresh, or, if the press comes as the device is
// going to sleep, wake again right away and do it then.

// Starts sampling the refresh keys. Call once per boot, as early as possible.
// A key already held down at this point (the press that woke the device, or a
// stuck key) is ignored until it has been released.
void x4c_refresh_keys_watch_start(void);

// Returns and clears a refresh-key press seen since the last call.
bool x4c_refresh_keys_take_press(void);

// Like x4c_refresh_keys_take_press(), but also counts a press that has only
// just started and is not debounced yet. For the last check before sleep.
bool x4c_refresh_keys_take_press_before_sleep(void);

// Remembers (in RTC memory) that the next wake must do a manual refresh.
void x4c_refresh_keys_defer_to_next_wake(void);

// Returns and clears the remembered manual refresh.
bool x4c_refresh_keys_take_deferred(void);
#endif // BOARD_XTEINK_X4_CLASSIC
