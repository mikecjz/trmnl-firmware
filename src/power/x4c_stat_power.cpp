#include <Arduino.h>
#include <power.h>

#ifdef INCLUDE_X4C_STAT_POWER

// GPIO21 is the X4 Classic's charge STAT line, active HIGH while the charger is
// actually charging (CrossPoint's freeink-sdk X4C profile and Papyrix's x4c
// profile both record `chargeStatusActiveHigh = true`).
X4CStatPower::X4CStatPower(uint8_t statPin) : _statPin(statPin) {}

UsbStatus X4CStatPower::usbStatus() {
  pinMode(_statPin, INPUT);
  // No PG/VBUS pin exists on this board; only a HIGH STAT proves USB power.
  return (digitalRead(_statPin) == HIGH) ? UsbStatus::CONNECTED : UsbStatus::UNKNOWN;
}

ChargingStatus X4CStatPower::chargingStatus() {
  pinMode(_statPin, INPUT);
  return (digitalRead(_statPin) == HIGH) ? ChargingStatus::CHARGING : ChargingStatus::NOT_CHARGING;
}

#endif // INCLUDE_X4C_STAT_POWER
