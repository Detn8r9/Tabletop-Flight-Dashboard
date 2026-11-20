#include "web.h"
#include <WebServer.h>
#include <WiFi.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "config.h"
#include "display.h"
#include "flights.h"
#include "basemap.h"
#include "net.h"
#include "audio.h"
#include "ui.h"
#include "tz.h"
#include "fmt.h"
#include "web_assets.h"   // generated from web/*.html by tools/embed_web.py

void onSettingsChanged(uint32_t chg);   // main.cpp
void requestReboot(uint32_t delayMs);   // main.cpp

static WebServer server(80);
static bool updateOk = false;
static String updateErr;

// Requests from the setup hotspot get the Wi-Fi page; from the home LAN, the app.
static bool viaAp() {
  return netApActive() && server.client().localIP() == WiFi.softAPIP();
}

// Simple CSRF guard: browsers can't add custom headers cross-site without a
// CORS preflight, which we never approve.
static bool guard() {
  if (server.header("X-FR") == "1") return true;
  server.send(403, "text/plain", "forbidden");
  return false;
}

static void sendGz(const uint8_t *data, size_t len, const char *type) {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, type, (const char *)data, len);
}

static void sendJson(JsonDocument &doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

static void handleRoot() {
  if (viaAp() || netState() == NET_SETUP) sendGz(SETUP_HTML_GZ, SETUP_HTML_GZ_LEN, "text/html");
  else sendGz(INDEX_HTML_GZ, INDEX_HTML_GZ_LEN, "text/html");
}

static void handleNotFound() {
  if (netApActive() && viaAp()) {
    // captive portal: bounce every unknown URL to the setup page
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "not found");
}

static void handleGetConfig() {
  JsonDocument doc;
  cfgLock();
  configToJson(cfg, doc.to<JsonObject>());
  cfgUnlock();
  sendJson(doc);
}

static void handlePostConfig() {
  if (!guard()) return;
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  cfgLock();
  uint32_t chg = configFromJson(cfg, in.as<JsonObjectConst>());
  cfgUnlock();
  configSave();
  onSettingsChanged(chg);
  handleGetConfig();
}

static void handleStatus() {
  JsonDocument doc;
  FlightStatus fs = flightsStatus();
  doc["fw"] = FW_VERSION;
  doc["ip"] = netIp();
  doc["host"] = netHost();
  doc["ssid"] = netSsid();
  doc["rssi"] = netRssi();
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  doc["psram"] = ESP.getFreePsram();
  doc["total"] = fs.total;
  doc["shown"] = uiPlanesOnScreen();   // matches the count on the device
  doc["source"] = sourceName(fs.activeSource);
  doc["dataAge"] = fs.lastOkMs ? (millis() - fs.lastOkMs) / 1000 : -1;
  doc["error"] = fs.error;
  doc["map"] = mapStatus();
  doc["seenToday"] = flightsSeenToday();
  time_t now = time(nullptr);
  doc["timeSet"] = now > 1700000000;
  sendJson(doc);
}

static void handleAircraft() {
  std::vector<Aircraft> list;
  flightsCopy(list);
  const uint8_t u = cfg.units;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  char b[32];
  for (const Aircraft &a : list) {
    if (!a.visible) continue;
    JsonObject o = arr.add<JsonObject>();
    o["hex"] = a.hex;
    flightName(b, sizeof b, a);
    o["name"] = b;
    o["model"] = modelName(a);
    o["call"] = a.call;
    o["reg"] = a.reg;
    o["type"] = a.type;
    o["cls"] = AC_CLASS_KEYS[a.cls];
    fmtAltitude(b, sizeof b, a, u);
    o["alt"] = b;
    fmtSpeed(b, sizeof b, a, u);
    o["speed"] = b;
    fmtDist(b, sizeof b, a.distNm, u);
    o["dist"] = b;
    o["dir"] = compass8(a.bearing);
    if (a.trackValid) o["heading"] = compass8(a.track);
    if (a.vsValid && abs(a.vs) >= 300 && !a.ground) o["vs"] = a.vs > 0 ? "climbing" : "descending";
    if (a.emergency) o["emerg"] = true;
    if (arr.size() >= 60) break;
  }
  sendJson(doc);
}

static void handleScreen() {
  static uint8_t *buf = (uint8_t *)heap_caps_malloc(SCREEN_W * SCREEN_H / 8, MALLOC_CAP_SPIRAM);
  displaySnapshot(buf);
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(SCREEN_W * SCREEN_H / 8);
  server.send(200, "application/octet-stream", "");
  server.sendContent((const char *)buf, SCREEN_W * SCREEN_H / 8);
}

static void handleTimezones() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < TZ_COUNT; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["n"] = TZ_TABLE[i].name;
    o["p"] = TZ_TABLE[i].posix;
  }
  sendJson(doc);
}

static void handleIpLocate() {
  double lat, lon;
  String city, tz;
  JsonDocument doc;
  if (netIpLocate(lat, lon, city, tz)) {
    doc["lat"] = lat;
    doc["lon"] = lon;
    doc["city"] = city;
    doc["tz"] = tz;
    sendJson(doc);
  } else {
    doc["error"] = "lookup failed";
    sendJson(doc, 502);
  }
}

static void handleScan() {
  int n = WiFi.scanNetworks(false, false);
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (JsonObject o : arr)
      if (o["ssid"] == s) { dup = true; break; }
    if (dup) continue;
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = s;
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  sendJson(doc);
}

static void handleWifi() {
  if (!guard()) return;
  JsonDocument in;
  if (deserializeJson(in, server.arg("plain")) || !in["ssid"].is<const char *>()) {
    server.send(400, "text/plain", "bad request");
    return;
  }
  netTryCredentials(in["ssid"].as<String>(), in["pass"] | "");
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleWifiStatus() {
  JsonDocument doc;
  String s = netTryStatus();
  doc["state"] = s;
  doc["host"] = netHost();
  sendJson(doc);
}

void webStartTask(void *) {
  for (;;) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void webStart() {
  const char *hdrs[] = {"X-FR"};
  server.collectHeaders(hdrs, 1);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/setup", HTTP_GET, [] { sendGz(SETUP_HTML_GZ, SETUP_HTML_GZ_LEN, "text/html"); });
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/aircraft", HTTP_GET, handleAircraft);
  server.on("/api/screen", HTTP_GET, handleScreen);
  server.on("/api/timezones", HTTP_GET, handleTimezones);
  server.on("/api/iplocate", HTTP_GET, handleIpLocate);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.on("/api/wifi/status", HTTP_GET, handleWifiStatus);
  server.on("/api/test-alert", HTTP_POST, [] {
    if (!guard()) return;
    audioChime(1, cfg.alertVolume);
    server.send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/api/reboot", HTTP_POST, [] {
    if (!guard()) return;
    server.send(200, "application/json", "{\"ok\":true}");
    requestReboot(800);
  });
  server.on("/api/reset", HTTP_POST, [] {
    if (!guard()) return;
    JsonDocument in;
    deserializeJson(in, server.arg("plain"));
    if (in["confirm"] != "RESET") {
      server.send(400, "text/plain", "confirmation missing");
      return;
    }
    factoryReset();
    server.send(200, "application/json", "{\"ok\":true}");
    requestReboot(800);
  });

  // Firmware upload (multipart). The page sends the X-FR header too.
  server.on(
      "/update", HTTP_POST,
      [] {
        if (!guard()) return;
        if (updateOk) {
          server.send(200, "application/json", "{\"ok\":true}");
          requestReboot(1000);
        } else {
          server.send(500, "application/json", "{\"ok\":false,\"error\":\"" + updateErr + "\"}");
        }
      },
      [] {
        HTTPUpload &up = server.upload();
        if (up.status == UPLOAD_FILE_START) {
          updateOk = false;
          updateErr = "";
          if (server.header("X-FR") != "1") { updateErr = "forbidden"; return; }
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) updateErr = Update.errorString();
        } else if (up.status == UPLOAD_FILE_WRITE) {
          if (!updateErr.length() && Update.write(up.buf, up.currentSize) != up.currentSize)
            updateErr = Update.errorString();
        } else if (up.status == UPLOAD_FILE_END) {
          if (!updateErr.length()) {
            if (Update.end(true)) updateOk = true;
            else updateErr = Update.errorString();
          }
        } else if (up.status == UPLOAD_FILE_ABORTED) {
          Update.abort();
          updateErr = "aborted";
        }
      });

  server.onNotFound(handleNotFound);
  server.begin();
  xTaskCreatePinnedToCore(webStartTask, "web", 8192, nullptr, 2, nullptr, 0);
}
