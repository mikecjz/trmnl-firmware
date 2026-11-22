#include <config.h>     // TCA9535_PG_PIN / TCA9535_STAT_PIN (TRMNL X)
#include <power.h>

#if defined(INCLUDE_X4C_STAT_POWER)
static X4CStatPower powerInstance(21); // GPIO21 charge STAT, active HIGH
#elif defined(INCLUDE_TCA9535_POWER)
// bbep is defined in display.cpp. On TRMNL X the BQ25616 PG/STAT lines are wired to the
// TCA9535 I/O expander, which is reached through the FastEPD io* helpers.
#include <epd_driver.h>

static TCA9535Power powerInstance(bbep, TCA9535_PG_PIN, TCA9535_STAT_PIN);
#elif defined(INCLUDE_GPIO_POWER)
static GPIOPower powerInstance(BQ25616_PG_PIN, BQ25616_STAT_PIN);
#else
static BasePower powerInstance;
#endif

BasePower &power() { return powerInstance; }
