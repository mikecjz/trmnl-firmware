#include <battery.h>
#include <config.h>

#if defined(BOARD_TRMNL_X)
static BQ27427Battery batteryInstance;
#elif defined(BOARD_XTEINK_X4_CLASSIC)
static Cw2017Battery batteryInstance;
#else
static ADCBattery batteryInstance;
#endif

#if defined(INCLUDE_BQ27427)
BQ27427Battery &battery() { return batteryInstance; }
#elif defined(INCLUDE_CW2017)
Cw2017Battery &battery() { return batteryInstance; }
#else
BaseBattery &battery() { return batteryInstance; }
#endif
