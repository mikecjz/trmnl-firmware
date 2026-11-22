#pragma once

#include <stdint.h>

#include "config.h"

//
// derive device-specific battery flags
//
#if defined(BOARD_TRMNL_X)
#define INCLUDE_BQ27427
#elif defined(BOARD_XTEINK_X4_CLASSIC)
#define INCLUDE_CW2017
#endif

/// @brief Abstract battery voltage source.
class BaseBattery {
public:
  virtual ~BaseBattery() = default;

  /// @brief Read the battery voltage.
  /// @return voltage in Volts, or a negative value if the reading failed
  virtual float readVoltage(TRMNL_DEVICE *pDevice) = 0;
  virtual float readVoltage() = 0;
};
/// @brief Reads the battery through a 1:2 resistor divider on an ADC pin.
class ADCBattery : public BaseBattery {
public:
  float readVoltage(TRMNL_DEVICE *pDevice) override;
  float readVoltage() override;
};

#ifdef INCLUDE_BQ27427
/// @brief Reads the battery voltage from the BQ27427 fuel gauge, and gathers
///        the full set of gas gauge readings (SOC, health, current, etc.).
class BQ27427Battery : public BaseBattery {
public:
  /// @brief Battery voltage from the last gaugeInit() snapshot.
  /// @return voltage in Volts, or a negative value if the last reading failed
  float readVoltage(TRMNL_DEVICE *pDevice) override { return 0.0f; }
  float readVoltage() override { return _voltage; } // V

  /// @brief Connects to the BQ27427 gas gauge, (re)configures it if needed,
  ///        and gathers a validated snapshot of its readings. Locks the
  ///        shared I2C bus for the duration.
  void gaugeInit();

  int readSoc() const { return _soc; }                     // %
  int readHealth() const { return _health; }                // %
  int readCurrent() const { return _current; }               // mA
  float readTemperature() const { return _temperature; }     // C
  int readCapacityRemain() const { return _capacityRemain; } // mAh
  int readCapacityFull() const { return _capacityFull; }     // mAh

  /// @brief Real gas-gauge (Impedance Track) readings, gathered alongside the
  ///        approximated values when BYPASS_BQ27427_SOC is active, purely for
  ///        comparison/data-gathering. Equal to the readSoc()/etc. values
  ///        above when the bypass isn't active.
  int readGaugeSoc() const { return _gaugeSoc; }                     // %
  int readGaugeHealth() const { return _gaugeHealth; }               // %
  int readGaugeCapacityRemain() const { return _gaugeCapacityRemain; } // mAh
  int readGaugeCapacityFull() const { return _gaugeCapacityFull; }     // mAh

private:
  int _soc = -1;
  int _health = -1;
  int _current = -1;
  float _temperature = -1;
  int _capacityRemain = -1;
  int _capacityFull = -1;
  float _voltage = -1;

  int _gaugeSoc = -1;
  int _gaugeHealth = -1;
  int _gaugeCapacityRemain = -1;
  int _gaugeCapacityFull = -1;
};
#endif // INCLUDE_BQ27427

#ifdef INCLUDE_CW2017
/// @brief Reads the CW2017 fuel gauge (I2C 0x63) on the Xteink X4 Classic.
///        The Classic keeps its factory calibration, so this driver never
///        rewrites the 80-byte BATINFO profile; it only validates the gauge and
///        reads VCELL. If the gauge does not answer, the last good reading is
///        reused; if there has never been one, a nominal 3.9 V is reported so
///        the device still boots and pairs, with an explicit warning in the log.
class Cw2017Battery : public BaseBattery {
public:
  float readVoltage(TRMNL_DEVICE *pDevice) override;
  float readVoltage() override { return _voltage; }

private:
  float _voltage = 3.9f;   // nominal fallback until the gauge answers
  bool _haveReading = false;
};
#endif

/// @brief Battery instance for the running board, selected at compile time.
#if defined(INCLUDE_BQ27427)
BQ27427Battery &battery();
#elif defined(INCLUDE_CW2017)
Cw2017Battery &battery();
#else
BaseBattery &battery();
#endif
