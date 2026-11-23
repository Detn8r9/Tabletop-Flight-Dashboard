#include "net.h"
#include "config.h"
#include "tz.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

void onLocationChanged();   // main.cpp

static NetState state = NET_SETUP;
static DNSServer dns;
static bool apOn = false;
static uint32_t stateSince = 0;
static uint32_t apStopAt = 0;
static uint32_t lastRetry = 0;
static bool servicesUp = false;
static String apName;

static String trySsid, tryPass;
static uint8_t tryState = 0;   // 0 idle, 1 connecting, 2 ok, 3 fail
static uint32_t tryStart = 0;

NetState netState() { return state; }
bool netApActive() { return apOn; }
String netIp() { return WiFi.isConnected() ? WiFi.localIP().toString() : String(""); }
String netHost() { return cfg.hostname + ".local"; }
String netApName() { return apName; }
int netRssi() { return WiFi.isConnected() ? WiFi.RSSI() : 0; }
String netSsid() {
  String s, p;
  wifiCredsLoad(s, p);
  return s;
}

static void setState(NetState s) {
  state = s;
  stateSince = millis();
}

static void startAp() {
  if (apOn) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName.c_str());   // open network: nothing secret travels over it but the Wi-Fi password, once
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", IPAddress(192, 168, 4, 1));   // captive portal: every name -> us
  apOn = true;
}

static void stopAp() {
  if (!apOn) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apOn = false;
}

void netApplyTimezone() {
  configTzTime(cfg.tzPosix.c_str(), "pool.ntp.org", "time.nist.gov", "time.google.com");
}

bool netIpLocate(double &lat, double &lon, String &city, String &tz) {
  HTTPClient http;
  WiFiClient c;
  http.begin(c, "http://ip-api.com/json/?fields=status,lat,lon,city,timezone");
  http.setTimeout(8000);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  JsonDocument doc;
  bool ok = !deserializeJson(doc, http.getString()) && doc["status"] == "success";
  http.end();
  if (!ok) return false;
  lat = doc["lat"];
  lon = doc["lon"];
  city = doc["city"].as<String>();
  tz = doc["timezone"].as<String>();
  return true;
}

static void autoLocateTask(void *) {
  double lat, lon;
  String city, tz;
  for (int i = 0; i < 3 && !cfg.locSet; i++) {
    if (netIpLocate(lat, lon, city, tz)) {
      cfgLock();
      bool apply = !cfg.locSet;  // the user may have set it meanwhile
      if (apply) {
        cfg.lat = lat;
        cfg.lon = lon;
        cfg.locName = city;
        cfg.locApprox = true;
        cfg.locSet = true;
        const char *px = tzLookup(tz.c_str());
        if (px) { cfg.tzName = tz; cfg.tzPosix = px; }
      }
      cfgUnlock();
      if (apply) {
        netApplyTimezone();
        configSave();
        onLocationChanged();
      }
      break;
    }
    delay(5000);
  }
  vTaskDelete(nullptr);
}

static void startServices() {
  if (servicesUp) return;
  servicesUp = true;
  MDNS.begin(cfg.hostname.c_str());
  MDNS.addService("http", "tcp", 80);
  ArduinoOTA.setHostname(cfg.hostname.c_str());
  ArduinoOTA.setMdnsEnabled(false);   // we already run mDNS
  ArduinoOTA.begin();
  netApplyTimezone();
  if (!cfg.locSet) xTaskCreate(autoLocateTask, "iploc", 6144, nullptr, 1, nullptr);
}

void netStart() {
  apName = AP_SSID_PREFIX;
  WiFi.persistent(false);
  WiFi.setHostname(cfg.hostname.c_str());
  WiFi.setAutoReconnect(true);

  String ssid, pass;
  if (wifiCredsLoad(ssid, pass)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    setState(NET_CONNECTING);
  } else {
    startAp();
    setState(NET_SETUP);
  }
}

void netTryCredentials(const String &ssid, const String &pass) {
  trySsid = ssid;
  tryPass = pass;
  tryState = 1;
  tryStart = millis();
  WiFi.disconnect(false);
  WiFi.begin(ssid.c_str(), pass.c_str());
}

String netTryStatus() {
  switch (tryState) {
    case 1: return "connecting";
    case 2: return "ok:" + netIp();
    case 3: return "fail";
  }
  return "idle";
}

void netLoop() {
  if (apOn) dns.processNextRequest();
  if (servicesUp) ArduinoOTA.handle();
  const bool up = WiFi.isConnected();
  const uint32_t now = millis();

  // setup page is testing new credentials
  if (tryState == 1) {
    if (up) {
      wifiCredsSave(trySsid, tryPass);
      tryState = 2;
      setState(NET_ONLINE);
      startServices();
      apStopAt = now + 20000;   // give the phone time to read the result
    } else if (now - tryStart > 25000) {
      tryState = 3;
      WiFi.disconnect(false);
    }
  }

  switch (state) {
    case NET_SETUP:
      break;
    case NET_CONNECTING:
      if (up) { setState(NET_ONLINE); startServices(); }
      else if (now - stateSince > 30000) { startAp(); setState(NET_FALLBACK); }
      break;
    case NET_FALLBACK:
      if (up && tryState != 1) {
        setState(NET_ONLINE);
        startServices();
        apStopAt = now + 10000;
      } else if (!up && tryState != 1 && now - lastRetry > 60000) {
        lastRetry = now;
        WiFi.reconnect();
      }
      break;
    case NET_ONLINE:
      if (up) stateSince = now;
      else if (now - stateSince > 5 * 60 * 1000UL) { startAp(); setState(NET_FALLBACK); }
      break;
  }

  if (apStopAt && now > apStopAt) {
    apStopAt = 0;
    if (WiFi.isConnected()) stopAp();
  }
}
