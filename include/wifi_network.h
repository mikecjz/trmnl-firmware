#ifndef WIFI_NETWORK_H
#define WIFI_NETWORK_H

#include <Arduino.h>

struct WiFiStatus {
  int rssi;    // signal strength in dBm
  String band; // "2.4" or "5"
};

/// @brief Resolve the current Wi-Fi status
WiFiStatus getWiFiStatus(void);

/// @brief Resolve the device hostname: custom hostname from preferences if set,
///        otherwise the board prefix plus friendly ID (e.g. "TRMNL-ABC123")
String getWifiClientHostname(void);

/// @brief Connect using credentials saved in WifiCaptivePortal
bool connectWithSavedCredentials(void);

/// @brief Ensure WiFi is connected, reconnecting with saved credentials if needed.
bool ensureWifiConnected(void);

#ifdef BOARD_XTEINK_X4_CLASSIC
/// @brief A manual (refresh-key) fetch failed: stop reusing the remembered DHCP
///        lease in case it went stale.
void x4cWifiManualFetchFailed(void);
#endif

#endif // WIFI_NETWORK_H
