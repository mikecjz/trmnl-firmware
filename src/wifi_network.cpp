#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WifiCaptive.h>
#include <config.h>
#include <trmnl_log.h>
#include <wifi_network.h>

#ifdef BOARD_TRMNL_X
#include <globals.h>

#include "modem.h"
#endif // BOARD_TRMNL_X

#ifdef BOARD_XTEINK_X4_CLASSIC
#include <globals.h>
#include <wifi-helpers.h>
#endif

namespace {
  constexpr unsigned long WIFI_RECONNECT_TIMEOUT_MS = 10000;
}

#ifdef BOARD_XTEINK_X4_CLASSIC
namespace {
  // The AP the last successful join landed on. RTC memory survives the deep
  // sleep between refreshes, so a manual-refresh wake can rejoin that AP
  // directly instead of paying the all-channel scan in connect.cpp (~2.4 s on
  // the measured unit). A stale entry only costs one short attempt: the caller
  // falls back to the normal scan-and-join path when this returns false.
  constexpr unsigned long X4C_FAST_REJOIN_TIMEOUT_MS = 5000;

  RTC_DATA_ATTR char x4c_last_ap_ssid[33] = {0};
  RTC_DATA_ATTR char x4c_last_ap_pswd[65] = {0};
  RTC_DATA_ATTR uint8_t x4c_last_ap_bssid[6] = {0};
  RTC_DATA_ATTR uint8_t x4c_last_ap_channel = 0;

  void rememberLastAp(const WifiCredentials &creds) {
    if (creds.ssid.length() == 0 || creds.is5GHz || creds.isEnterprise) return;
    const uint8_t *bssid = WiFi.BSSID();
    const int channel = WiFi.channel();
    if (bssid == nullptr || channel <= 0) return;

    strncpy(x4c_last_ap_ssid, creds.ssid.c_str(), sizeof(x4c_last_ap_ssid) - 1);
    x4c_last_ap_ssid[sizeof(x4c_last_ap_ssid) - 1] = '\0';
    strncpy(x4c_last_ap_pswd, creds.pswd.c_str(), sizeof(x4c_last_ap_pswd) - 1);
    x4c_last_ap_pswd[sizeof(x4c_last_ap_pswd) - 1] = '\0';
    memcpy(x4c_last_ap_bssid, bssid, sizeof(x4c_last_ap_bssid));
    x4c_last_ap_channel = (uint8_t)channel;
  }

  bool connectToRememberedAp(const WifiCredentials &creds) {
    if (x4c_last_ap_channel == 0) return false;
    // Static IP and enterprise setups keep the full path: they need the
    // options connect.cpp applies around WiFi.begin().
    if (creds.useStaticIP || creds.is5GHz || creds.isEnterprise) return false;
    if (creds.ssid != x4c_last_ap_ssid || creds.pswd != x4c_last_ap_pswd) return false;

    Log_info("WiFi: manual refresh - rejoining %s on channel %d without a scan", creds.ssid.c_str(),
             x4c_last_ap_channel);
    unsigned long started = millis();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(0);
    WiFi.setMinSecurity(WIFI_AUTH_OPEN);
    WiFi.setAutoReconnect(false);
    applyWifiHostname(WifiCaptivePortal.getHostname());
    WiFi.begin(creds.ssid.c_str(), creds.pswd.c_str(), x4c_last_ap_channel, x4c_last_ap_bssid);

    while (WiFi.status() != WL_CONNECTED && millis() - started < X4C_FAST_REJOIN_TIMEOUT_MS) {
      delay(50);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Log_info("WiFi: rejoined remembered AP in %lu ms", (unsigned long)(millis() - started));
      return true;
    }

    Log_info("WiFi: remembered AP did not answer in %lu ms; falling back to a scan",
             (unsigned long)(millis() - started));
    WiFi.disconnect();
    return false;
  }
} // namespace
#endif // BOARD_XTEINK_X4_CLASSIC

String getWifiClientHostname(void) {
  Preferences prefs;
  if (!prefs.begin("data", true)) return String(WIFI_CLIENT_HOSTNAME_PREFIX);

  String customHostName = prefs.getString(PREFERENCES_HOSTNAME, "");
  if (customHostName.length() > 0) {
    prefs.end();
    return customHostName;
  }

  String hostname = WIFI_CLIENT_HOSTNAME_PREFIX;
  String friendly_id = prefs.getString(PREFERENCES_FRIENDLY_ID, PREFERENCES_FRIENDLY_ID_DEFAULT);
  prefs.end();
  if (friendly_id.length() > 0) hostname += "-" + friendly_id;
  return hostname;
}

WiFiStatus getWiFiStatus(void) {
  WiFiStatus status;
  status.rssi = WiFi.RSSI();
  status.band = WiFi.channel() >= 36 ? "5" : "2.4";

#ifdef BOARD_TRMNL_X
  // On the 5 GHz path the ESP32-C5 modem owns the Wi-Fi link, so the host's
  // WiFi.RSSI() reads 0; query the modem for the real signal strength.
  if (g_modem && WifiCaptivePortal.getLastCredentials().is5GHz) {
    status.rssi = g_modem->getSignalRssi();
    status.band = "5";
  }
#endif // BOARD_TRMNL_X

  return status;
}

static void startWifiMdns(void) {
  String hostname = getWifiClientHostname();
  if (MDNS.begin(hostname.c_str())) {
    Log_info("mDNS started: %s.local", hostname.c_str());
  } else {
    Log_error("mDNS failed to start");
  }
}

bool connectWithSavedCredentials(void) {
  if (!WifiCaptivePortal.isSaved()) return false;

#ifdef BOARD_TRMNL_X
  if (g_modem) {
    WifiCredentials creds = WifiCaptivePortal.getLastCredentials();
    if (creds.is5GHz && !creds.isEnterprise)
      return g_modem->connectToNetwork(creds.ssid, creds.pswd, getWifiClientHostname());
  }
#endif

#ifdef BOARD_XTEINK_X4_CLASSIC
  // A refresh-key wake sets force_full_refresh. The user is standing at the
  // device and wants the screen, so rejoin the AP that worked minutes ago
  // directly rather than scanning every channel for a possibly stronger one.
  if (force_full_refresh && connectToRememberedAp(WifiCaptivePortal.getLastCredentials())) {
    startWifiMdns();
    return true;
  }
#endif

  bool connected = WifiCaptivePortal.autoConnect();
  if (connected) {
#ifdef BOARD_XTEINK_X4_CLASSIC
    rememberLastAp(WifiCaptivePortal.getLastCredentials());
#endif
    startWifiMdns();
  }
  return connected;
}

bool ensureWifiConnected(void) {
  if (WiFi.status() == WL_CONNECTED) return true;

  Log_info("%s [%d]: WiFi disconnected, reconnecting...\r\n", __FILE__, __LINE__);

  if (!WifiCaptivePortal.isSaved()) {
    Log_error("%s [%d]: No saved WiFi credentials\r\n", __FILE__, __LINE__);
    return false;
  }

  WiFi.reconnect();
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_RECONNECT_TIMEOUT_MS)
    delay(100);

  if (WiFi.status() == WL_CONNECTED) return true;

  return connectWithSavedCredentials();
}
