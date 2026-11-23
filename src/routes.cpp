#include "routes.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

struct Entry {
  char call[9];
  bool found;
  RouteInfo r;
  uint32_t ms;     // when looked up
  uint32_t used;   // last read, for eviction
};

static const int CAP = 160;
static Entry *cache = nullptr;
static SemaphoreHandle_t lock;
static char queue[12][9];
static int qLen = 0;

static Entry *findLocked(const char *call) {
  for (int i = 0; i < CAP; i++)
    if (cache[i].call[0] && !strcmp(cache[i].call, call)) return &cache[i];
  return nullptr;
}

// Only airline-style callsigns (ABC1234) have routes; private planes use
// their registration as callsign.
static bool looksLikeFlight(const char *c) {
  size_t n = strlen(c);
  return n >= 4 && isalpha(c[0]) && isalpha(c[1]) && isalpha(c[2]) && isdigit(c[3]);
}

void routesWant(const char *call) {
  if (!cache || !looksLikeFlight(call)) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  Entry *e = findLocked(call);
  // answers are kept 12 h; "not found" is retried after 3 h
  bool fresh = e && millis() - e->ms < (e->found ? 12UL : 3UL) * 3600000UL;
  bool queued = false;
  for (int i = 0; i < qLen; i++) if (!strcmp(queue[i], call)) queued = true;
  if (!fresh && !queued && qLen < 12) strlcpy(queue[qLen++], call, 9);
  xSemaphoreGive(lock);
}

bool routeFor(const char *call, RouteInfo &out) {
  if (!cache || !call[0]) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  Entry *e = findLocked(call);
  bool ok = e && e->found;
  if (ok) { out = e->r; e->used = millis(); }
  xSemaphoreGive(lock);
  return ok;
}

int routesCached() {
  int n = 0;
  for (int i = 0; cache && i < CAP; i++) n += cache[i].call[0] && cache[i].found;
  return n;
}

static void cityOf(JsonVariantConst ap, char *dst, size_t n) {
  const char *c = ap["municipality"] | "";
  if (!c[0]) c = ap["name"] | "";
  strlcpy(dst, c, n);
}

static bool lookup(const char *call, RouteInfo &r, bool &found) {
  char url[96];
  snprintf(url, sizeof url, "https://api.adsbdb.com/v0/callsign/%s", call);
  WiFiClientSecure c;
  c.setInsecure();
  HTTPClient http;
  if (!http.begin(c, url)) return false;
  http.setTimeout(8000);
  http.setUserAgent("FlightRadar-ESP32/" FW_VERSION);
  int code = http.GET();
  if (code == 404) { found = false; http.end(); return true; }   // unknown callsign
  if (code != 200) { http.end(); return false; }
  String body = http.getString();
  http.end();
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonVariantConst fr = doc["response"]["flightroute"];
  if (fr.isNull()) { found = false; return true; }
  strlcpy(r.from, fr["origin"]["iata_code"] | "", sizeof r.from);
  strlcpy(r.to, fr["destination"]["iata_code"] | "", sizeof r.to);
  cityOf(fr["origin"], r.fromCity, sizeof r.fromCity);
  cityOf(fr["destination"], r.toCity, sizeof r.toCity);
  found = r.from[0] && r.to[0];
  return true;
}

static void store(const char *call, bool found, const RouteInfo &r) {
  xSemaphoreTake(lock, portMAX_DELAY);
  Entry *e = findLocked(call);
  if (!e) {   // evict the least recently used slot
    e = &cache[0];
    for (int i = 0; i < CAP; i++) {
      if (!cache[i].call[0]) { e = &cache[i]; break; }
      if (cache[i].used < e->used) e = &cache[i];
    }
  }
  strlcpy(e->call, call, sizeof e->call);
  e->found = found;
  e->r = r;
  e->ms = e->used = millis();
  xSemaphoreGive(lock);
}

static void routesTask(void *) {
  int failures = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1200));   // be gentle with the free service
    if (!cfg.routesOn || WiFi.status() != WL_CONNECTED) continue;
    char call[9] = "";
    xSemaphoreTake(lock, portMAX_DELAY);
    if (qLen) {
      strlcpy(call, queue[0], sizeof call);
      memmove(queue[0], queue[1], (qLen - 1) * sizeof queue[0]);
      qLen--;
    }
    xSemaphoreGive(lock);
    if (!call[0]) continue;
    RouteInfo r;
    bool found = false;
    if (lookup(call, r, found)) {
      store(call, found, r);
      failures = 0;
    } else if (++failures > 5) {
      vTaskDelay(pdMS_TO_TICKS(60000));   // service down: back off for a minute
      failures = 0;
    }
  }
}

void routesStart() {
  lock = xSemaphoreCreateMutex();
  cache = (Entry *)heap_caps_calloc(CAP, sizeof(Entry), MALLOC_CAP_SPIRAM);
  xTaskCreatePinnedToCore(routesTask, "routes", 8192, nullptr, 1, nullptr, 0);
}
