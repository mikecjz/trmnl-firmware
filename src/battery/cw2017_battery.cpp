// Only the Xteink X4 Classic build selects this driver.
#if defined(BOARD_XTEINK_X4_CLASSIC)

// CW2017 fuel gauge (I2C 0x63) used by the Xteink X4 Classic.
//
// Register map and the "keep the factory calibration" behaviour follow the
// MIT-licensed Papyrix Cw2017Backend.cpp. On the Classic Papyrix deliberately
// passes a null BATINFO profile: the gauge is never rewritten, only validated
// (VERSION must report running) and read (VCELL). That is what this driver does.
//
// Failure policy: reuse the last good reading; if the gauge has never answered,
// keep the nominal 3.9 V initial value so the device still boots, pairs and can
// be diagnosed. A warning is logged once per boot.

#include <Arduino.h>
#include <ArduinoLog.h>
#include <Wire.h>
#include <battery.h>
#include <config.h>

namespace {

  constexpr uint8_t kGaugeAddress = 0x63;
  constexpr uint8_t kRegVersion = 0x00;
  constexpr uint8_t kRegVcell = 0x02; // two bytes, MSB first
  constexpr uint32_t kI2cHz = 400000;

  bool cw2017VersionIsRunning(uint8_t version) { return (version & 0xFD) == 0x0D; }

// raw is a 14-bit ADC value; 5/16 scaling gives millivolts (Papyrix/Datasheet).
  uint16_t cw2017Millivolts(uint16_t raw) {
    const uint16_t raw14 = static_cast<uint16_t>(raw & 0x3FFF);
    return static_cast<uint16_t>((raw14 * 5U + 8U) >> 4U);
  }

  bool readReg8(uint8_t reg, uint8_t *value) {
    Wire.beginTransmission(kGaugeAddress);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom(static_cast<int>(kGaugeAddress), 1) != 1) return false;
    *value = Wire.read();
    return true;
  }

  bool readReg16Be(uint8_t reg, uint16_t *value) {
    Wire.beginTransmission(kGaugeAddress);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    if (Wire.requestFrom(static_cast<int>(kGaugeAddress), 2) != 2) return false;
    const uint8_t high = Wire.read();
    const uint8_t low = Wire.read();
    *value = static_cast<uint16_t>((high << 8) | low);
    return true;
  }

} // namespace

float Cw2017Battery::readVoltage(TRMNL_DEVICE *pDevice) {
  if (pDevice == nullptr || pDevice->batt_type != BATT_CW2017) return _voltage;
  if (pDevice->sensor_sda == 0xff || pDevice->sensor_scl == 0xff) {
    Log.warning("CW2017: no I2C pins configured; keeping %.2f V", _voltage);
    return _voltage;
  }

  if (!Wire.begin(pDevice->sensor_sda, pDevice->sensor_scl, kI2cHz)) {
    Log.warning("CW2017: I2C begin failed; keeping %.2f V", _voltage);
    return _voltage;
  }
  Wire.setTimeOut(10);

  // One retry covers the gauge waking with the rest of the board.
  for (int attempt = 0; attempt < 2; ++attempt) {
    uint8_t version = 0;
    uint16_t raw = 0;
    if (readReg8(kRegVersion, &version) && cw2017VersionIsRunning(version) && readReg16Be(kRegVcell, &raw)) {
      const uint16_t millivolts = cw2017Millivolts(raw);
      if (millivolts >= 2500 && millivolts <= 4500) {
        _voltage = static_cast<float>(millivolts) / 1000.0f;
        _haveReading = true;
        Log.info("CW2017: %.2f V (VCELL=0x%04X)", _voltage, raw);
        return _voltage;
      }
      Log.warning("CW2017: implausible VCELL=0x%04X (%u mV)", raw, millivolts);
    }
    delay(20);
  }

  if (!_haveReading) {
    Log.warning("CW2017: gauge did not answer; reporting nominal %.2f V", _voltage);
  }
  return _voltage;
}

#endif // BOARD_XTEINK_X4_CLASSIC
