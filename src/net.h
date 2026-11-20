// Wi-Fi: first-run setup hotspot with captive portal, home network
// connection, mDNS name, clock sync, OTA updates.
#pragma once
#include <Arduino.h>

enum NetState : uint8_t {
  NET_SETUP,        // no saved Wi-Fi: hotspot + setup page
  NET_CONNECTING,   // trying the saved network
  NET_ONLINE,       // on the home network
  NET_FALLBACK      // saved network unreachable: hotspot open again
};

void netStart();
void netLoop();                 // call often (DNS, OTA, state changes)
NetState netState();
bool netApActive();
String netIp();
String netHost();               // e.g. "flightradar.local"
String netSsid();               // saved home network name
String netApName();
int netRssi();

// Setup page actions
void netTryCredentials(const String &ssid, const String &pass);
String netTryStatus();          // "idle", "connecting", "ok:<ip>", "fail"

// One-off approximate location from the public IP (ip-api.com).
bool netIpLocate(double &lat, double &lon, String &city, String &tz);
void netApplyTimezone();
