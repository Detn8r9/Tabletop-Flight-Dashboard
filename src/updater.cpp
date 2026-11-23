#include "updater.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

static SemaphoreHandle_t lock;
static String status = "not checked yet";
static String latest;
static volatile bool busy = false;
static TaskHandle_t task = nullptr;
static volatile bool wantInstall = false;

bool updaterBusy() { return busy; }

static void setStatus(const String &s) {
  xSemaphoreTake(lock, portMAX_DELAY);
  status = s;
  xSemaphoreGive(lock);
}
String updaterStatus() {
  xSemaphoreTake(lock, portMAX_DELAY);
  String s = status;
  xSemaphoreGive(lock);
  return s;
}
String updaterLatest() {
  xSemaphoreTake(lock, portMAX_DELAY);
  String s = latest;
  xSemaphoreGive(lock);
  return s;
}

bool versionNewer(const char *a, const char *b) {
  if (*a == 'v' || *a == 'V') a++;
  if (*b == 'v' || *b == 'V') b++;
  for (int i = 0; i < 3; i++) {
    long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
    if (x != y) return x > y;
    if (*a == '.') a++;
    if (*b == '.') b++;
  }
  return false;
}

static String repo() {
  String r = cfgSnapshot().updateRepo;
  r.trim();
  if (!r.length()) r = DEFAULT_UPDATE_REPO;
  return r;
}

// Asks GitHub for the latest release. Returns false on failure.
static bool fetchLatest(String &tag, String &url) {
  String r = repo();
  if (r.indexOf('/') < 1) { setStatus("no GitHub repository set"); return false; }
  WiFiClientSecure c;
  c.setInsecure();
  HTTPClient http;
  http.begin(c, "https://api.github.com/repos/" + r + "/releases/latest");
  http.setUserAgent("FlightRadar-ESP32/" FW_VERSION);   // GitHub requires one
  http.addHeader("Accept", "application/vnd.github+json");
  http.setTimeout(15000);
  int code = http.GET();
  if (code != 200) {
    setStatus(code == 404 ? "no releases found in " + r : "GitHub said " + String(code));
    http.end();
    return false;
  }
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, http.getString(), DeserializationOption::Filter(filter));
  http.end();
  if (e) { setStatus("bad reply from GitHub"); return false; }
  tag = doc["tag_name"].as<String>();
  url = "";
  for (JsonObjectConst a : doc["assets"].as<JsonArrayConst>())
    if (a["name"] == "firmware.bin") url = a["browser_download_url"].as<String>();
  return tag.length() > 0;
}

static bool install(const String &url, const String &tag) {
  busy = true;
  setStatus("installing " + tag + "...");
  delay(500);   // let the screen show the notice
  WiFiClientSecure c;
  c.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);   // assets redirect to a CDN
  httpUpdate.rebootOnUpdate(true);
  t_httpUpdate_return r = httpUpdate.update(c, url);
  // only returns on failure (success reboots)
  busy = false;
  setStatus(r == HTTP_UPDATE_NO_UPDATES ? "already up to date" : "update failed: " + httpUpdate.getLastErrorString());
  return false;
}

static void check(bool doInstall) {
  if (WiFi.status() != WL_CONNECTED) { setStatus("offline"); return; }
  setStatus("checking...");
  String tag, url;
  if (!fetchLatest(tag, url)) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  latest = tag;
  xSemaphoreGive(lock);
  if (!versionNewer(tag.c_str(), FW_VERSION)) { setStatus("up to date (" FW_VERSION ")"); return; }
  if (!url.length()) { setStatus(tag + " has no firmware.bin"); return; }
  if (!doInstall) { setStatus(tag + " is available"); return; }
  install(url, tag);
}

static void updaterTask(void *) {
  // first check 10 minutes after start, then once a day
  uint32_t wait = 10 * 60 * 1000;
  for (;;) {
    bool manual = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait)) > 0;
    wait = 24 * 3600 * 1000UL;
    if (manual) { check(wantInstall); continue; }
    check(cfgSnapshot().autoUpdate);
  }
}

void updaterCheckNow(bool doInstall) {
  wantInstall = doInstall;
  if (task) xTaskNotifyGive(task);
}

void updaterStart() {
  lock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(updaterTask, "updater", 10 * 1024, nullptr, 1, &task, 0);
}
