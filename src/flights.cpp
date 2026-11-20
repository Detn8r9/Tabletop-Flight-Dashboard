#include "flights.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "audio.h"
#include "names.h"
#include <atomic>

// ---------- PSRAM allocator so big JSON responses don't exhaust internal RAM
struct SpiRamAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override { return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM); }
};
static SpiRamAllocator psramAlloc;

static SemaphoreHandle_t lock;
static std::vector<Aircraft> current;
static FlightStatus status;
static std::atomic<uint32_t> version{0};
static TaskHandle_t task = nullptr;

// alert queue (single slot is enough; newest wins)
static AlertEvent pendingAlert;
static bool alertWaiting = false;
struct Alerted { char hex[7]; uint32_t ms; };
static Alerted alerted[32];
static int alertedIdx = 0;

const char *sourceName(uint8_t src) {
  switch (src) {
    default:
    case SRC_ADSBLOL: return "adsb.lol";
    case SRC_ADSBFI: return "adsb.fi";
    case SRC_AIRPLANESLIVE: return "airplanes.live";
  }
}

static String buildUrl(uint8_t src, double lat, double lon, int radius) {
  char buf[160];
  switch (src) {
    default:
    case SRC_ADSBLOL:
      snprintf(buf, sizeof buf, "https://api.adsb.lol/v2/lat/%.4f/lon/%.4f/dist/%d", lat, lon, radius);
      break;
    case SRC_ADSBFI:
      snprintf(buf, sizeof buf, "https://opendata.adsb.fi/api/v2/lat/%.4f/lon/%.4f/dist/%d", lat, lon, radius);
      break;
    case SRC_AIRPLANESLIVE:
      snprintf(buf, sizeof buf, "https://api.airplanes.live/v2/point/%.4f/%.4f/%d", lat, lon, radius);
  }
  return buf;
}

// ---------- pure helpers

void geoDistBearing(double lat1, double lon1, double lat2, double lon2, float &nm, float &brg) {
  const double R = 3440.065;  // earth radius in nautical miles
  double p1 = radians(lat1), p2 = radians(lat2), dl = radians(lon2 - lon1);
  double a = sin((p2 - p1) / 2) * sin((p2 - p1) / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  nm = (float)(2 * R * atan2(sqrt(a), sqrt(1 - a)));
  double y = sin(dl) * cos(p2);
  double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double b = degrees(atan2(y, x));
  brg = (float)fmod(b + 360.0, 360.0);
}

uint8_t classify(const char *cat, uint8_t dbFlags) {
  if (dbFlags & 1) return CLS_MILITARY;
  if (!cat || !cat[0]) return CLS_UNKNOWN;
  if (cat[0] == 'A') {
    switch (cat[1]) {
      case '1': return CLS_LIGHT;
      case '2': return CLS_REGIONAL;
      case '3': case '4': case '5': return CLS_AIRLINER;
      case '6': return CLS_HIGHPERF;
      case '7': return CLS_HELI;
    }
  } else if (cat[0] == 'B') {
    switch (cat[1]) {
      case '1': case '4': return CLS_GLIDER;
      case '2': return CLS_BALLOON;
      case '6': return CLS_DRONE;
    }
  }
  return CLS_UNKNOWN;
}

// Case-insensitive glob ('*' any run, '?' one char)
static bool glob(const char *p, const char *s) {
  if (!*p) return !*s;
  if (*p == '*') return glob(p + 1, s) || (*s && glob(p, s + 1));
  if (*s && (*p == '?' || toupper(*p) == toupper(*s))) return glob(p + 1, s + 1);
  return false;
}

bool matchTypeList(const char *type, const String &list) {
  if (!type || !type[0] || !list.length()) return false;
  int start = 0;
  while (start < (int)list.length()) {
    int end = list.indexOf(',', start);
    if (end < 0) end = list.length();
    String tok = list.substring(start, end);
    tok.trim();
    if (tok.length() && glob(tok.c_str(), type)) return true;
    start = end + 1;
  }
  return false;
}

static bool matchPrefixList(const char *call, const String &list) {
  if (!call || !call[0]) return false;
  int start = 0;
  while (start < (int)list.length()) {
    int end = list.indexOf(',', start);
    if (end < 0) end = list.length();
    String tok = list.substring(start, end);
    tok.trim();
    if (tok.length() && strncasecmp(call, tok.c_str(), tok.length()) == 0) return true;
    start = end + 1;
  }
  return false;
}

bool passesFilter(const Aircraft &a, const Settings &s) {
  if (a.distNm > s.rangeNm * RANGE_TO_CORNER) return false;
  if (a.emergency && s.emergAlways) return true;
  if (!s.classOn[a.cls]) return false;
  if (s.hideGround && a.ground) return false;
  if (s.altFilter && !a.ground) {
    if (!a.altValid) return false;
    if (a.alt < s.altMin || a.alt > s.altMax) return false;
  }
  if (s.modelMode == MODEL_ONLY && !matchTypeList(a.type, s.models)) return false;
  if (s.modelMode == MODEL_HIDE && matchTypeList(a.type, s.models)) return false;
  if (s.callFilter && s.callPrefixes.length() && !matchPrefixList(a.call, s.callPrefixes)) return false;
  return true;
}

// ---------- alerts

static bool recentlyAlerted(const char *hex) {
  uint32_t now = millis();
  for (auto &r : alerted)
    if (r.hex[0] && !strcmp(r.hex, hex) && now - r.ms < 3600UL * 1000) return true;
  return false;
}

static void checkAlerts(const Settings &cfg, const std::vector<Aircraft> &list) {
  if (!cfg.alertsOn) return;
  for (const Aircraft &a : list) {
    if (a.distNm > cfg.rangeNm) continue;
    const char *why = nullptr;
    if (cfg.alertEmerg && a.emergency) why = "Emergency";
    else if (cfg.alertMil && a.cls == CLS_MILITARY) why = "Military";
    else if (cfg.alertHeli && a.cls == CLS_HELI) why = "Helicopter";
    else if (cfg.alertTypes && matchTypeList(a.type, cfg.alertTypeList)) why = "Watched";
    else if (cfg.alertNear && a.distNm <= cfg.alertNearNm && !a.ground) why = "Overhead";
    if (!why || recentlyAlerted(a.hex)) continue;

    strlcpy(alerted[alertedIdx].hex, a.hex, 7);
    alerted[alertedIdx].ms = millis();
    alertedIdx = (alertedIdx + 1) % 32;

    xSemaphoreTake(lock, portMAX_DELAY);
    char name[24];
    if (a.call[0]) friendlyFlight(a.call, name, sizeof name);
    else snprintf(name, sizeof name, "%s", a.reg[0] ? a.reg : a.hex);
    const char *model = aircraftModel(a.type);
    snprintf(pendingAlert.text, sizeof pendingAlert.text, "%s: %s %s", why, name, model ? model : "");
    pendingAlert.ms = millis();
    alertWaiting = true;
    xSemaphoreGive(lock);
    if (cfg.alertSound) audioChime(a.emergency ? 2 : 1, cfg.alertVolume);
    break;  // one alert per refresh is plenty
  }
}

bool alertsPending(AlertEvent &out) {
  bool r = false;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (alertWaiting) { out = pendingAlert; alertWaiting = false; r = true; }
  xSemaphoreGive(lock);
  return r;
}

// ---------- fetching

// A write-only Stream that collects a download in PSRAM.
struct PsramBuffer : public Stream {
  uint8_t *data = nullptr;
  size_t len = 0, cap = 0;
  bool overflow = false;
  static const size_t MAX = 3 * 1024 * 1024;
  void clear() { len = 0; overflow = false; }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *buf, size_t n) override {
    if (len + n > cap) {
      size_t nc = max(cap ? cap * 2 : (size_t)256 * 1024, len + n);
      if (nc > MAX) { overflow = true; return 0; }
      uint8_t *p = (uint8_t *)heap_caps_realloc(data, nc, MALLOC_CAP_SPIRAM);
      if (!p) { overflow = true; return 0; }
      data = p;
      cap = nc;
    }
    memcpy(data + len, buf, n);
    len += n;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
};

static void copyStr(char *dst, size_t n, JsonVariantConst v) {
  const char *s = v.as<const char *>();
  if (!s) { dst[0] = 0; return; }
  while (*s == ' ') s++;
  strlcpy(dst, s, n);
  // trim trailing spaces (callsigns are space padded)
  for (int i = strlen(dst) - 1; i >= 0 && dst[i] == ' '; i--) dst[i] = 0;
}

static bool fetchOnce(const Settings &cfg, uint8_t src, String &err, std::vector<Aircraft> &out, int &total) {
  // the map reaches past the range ring into the screen corners; cap the
  // request so busy areas don't produce multi-megabyte replies
  int radius = (int)ceilf(min(MAX_FETCH_NM, cfg.rangeNm * RANGE_TO_CORNER + 1));
  String url = buildUrl(src, cfg.lat, cfg.lon, radius);

  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  bool ok;
  if (url.startsWith("https")) {
    secure.setInsecure();   // public, read-only data
    ok = http.begin(secure, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) { err = "bad URL"; return false; }
  http.setTimeout(15000);
  http.setUserAgent("FlightRadar-ESP32/" FW_VERSION);
  int code = http.GET();
  if (code != 200) {
    err = code < 0 ? http.errorToString(code) : ("HTTP " + String(code));
    if (code >= 400 && code < 500) err += " (refused)";
    http.end();
    return false;
  }

  JsonDocument filter;
  for (const char *k : {"ac", "aircraft"}) {
    JsonObject f = filter[k][0].to<JsonObject>();
    for (const char *fld : {"hex", "flight", "r", "t", "category", "alt_baro", "gs", "track",
                            "true_heading", "baro_rate", "geom_rate", "lat", "lon", "squawk",
                            "emergency", "dbFlags"})
      f[fld] = true;
  }
  // Download the whole reply first (HTTPClient undoes chunked encoding),
  // then parse from memory. Parsing straight off a TLS stream gave up at
  // the first pause in the transfer.
  static PsramBuffer body;
  body.clear();
  int wrote = http.writeToStream(&body);
  http.end();
  if (wrote < 0) { err = "download " + HTTPClient::errorToString(wrote); return false; }
  if (body.overflow) { err = "reply too large"; return false; }
  JsonDocument doc(&psramAlloc);
  DeserializationError de = deserializeJson(doc, (const char *)body.data, body.len,
                                            DeserializationOption::Filter(filter),
                                            DeserializationOption::NestingLimit(6));
  if (de) { err = String("JSON ") + de.c_str() + " (" + String(body.len / 1024) + " KB)"; return false; }

  JsonArrayConst arr = doc["ac"].isNull() ? doc["aircraft"].as<JsonArrayConst>() : doc["ac"].as<JsonArrayConst>();
  out.clear();
  out.reserve(arr.size());
  total = 0;
  for (JsonObjectConst o : arr) {
    if (o["lat"].isNull() || o["lon"].isNull()) continue;
    Aircraft a;
    copyStr(a.hex, sizeof a.hex, o["hex"]);
    copyStr(a.call, sizeof a.call, o["flight"]);
    copyStr(a.reg, sizeof a.reg, o["r"]);
    copyStr(a.type, sizeof a.type, o["t"]);
    copyStr(a.cat, sizeof a.cat, o["category"]);
    copyStr(a.squawk, sizeof a.squawk, o["squawk"]);
    JsonVariantConst alt = o["alt_baro"];
    if (alt.is<const char *>()) { a.ground = !strcmp(alt.as<const char *>(), "ground"); a.alt = 0; a.altValid = a.ground; }
    else if (!alt.isNull()) { a.alt = alt.as<int>(); a.altValid = true; }
    if (!o["gs"].isNull()) { a.gs = o["gs"].as<float>(); a.gsValid = true; }
    if (!o["track"].isNull()) { a.track = o["track"].as<float>(); a.trackValid = true; }
    else if (!o["true_heading"].isNull()) { a.track = o["true_heading"].as<float>(); a.trackValid = true; }
    JsonVariantConst vr = o["baro_rate"].isNull() ? o["geom_rate"] : o["baro_rate"];
    if (!vr.isNull()) { a.vs = vr.as<int>(); a.vsValid = true; }
    a.lat = o["lat"].as<double>();
    a.lon = o["lon"].as<double>();
    a.dbFlags = o["dbFlags"] | 0;
    const char *em = o["emergency"] | "none";
    a.emergency = strcmp(em, "none") != 0 || !strcmp(a.squawk, "7500") ||
                  !strcmp(a.squawk, "7600") || !strcmp(a.squawk, "7700");
    a.cls = classify(a.cat, a.dbFlags);
    geoDistBearing(cfg.lat, cfg.lon, a.lat, a.lon, a.distNm, a.bearing);
    a.visible = passesFilter(a, cfg);
    out.push_back(a);
    total++;
  }
  // nearest first: makes the side panel and label priority trivial
  std::sort(out.begin(), out.end(), [](const Aircraft &x, const Aircraft &y) { return x.distNm < y.distNm; });
  return true;
}

static void noteSeen(const std::vector<Aircraft> &list);

static void fetchTask(void *) {
  std::vector<Aircraft> fresh;
  for (;;) {
    const Settings cfg = cfgSnapshot();
    uint32_t waitMs = (uint32_t)(cfg.refreshSec * 1000);
    if (WiFi.status() == WL_CONNECTED && cfg.locSet) {
      xSemaphoreTake(lock, portMAX_DELAY);
      uint8_t src = status.activeSource;
      xSemaphoreGive(lock);
      String err;
      int total = 0;
      uint32_t t0 = millis();
      bool ok = fetchOnce(cfg, src, err, fresh, total);
      xSemaphoreTake(lock, portMAX_DELAY);
      status.lastTryMs = millis();
      if (ok) {
        current.swap(fresh);
        status.lastOkMs = millis();
        status.total = total;
        status.shown = 0;
        for (auto &a : current) status.shown += a.visible;
        status.failures = 0;
        status.error = "";
        version++;
        // running on a fallback source: retry the preferred one now and then
        static int okOnFallback = 0;
        if (src != cfg.source && ++okOnFallback > 30) { okOnFallback = 0; status.activeSource = cfg.source; }
      } else {
        status.failures++;
        status.error = String(sourceName(src)) + ": " + err;
        // a refusal (4xx) moves to the next source straight away; other
        // errors after 2 tries. The preferred source is retried later.
        if (err.endsWith("(refused)") || status.failures % 2 == 0) status.activeSource = (src + 1) % SRC_COUNT;
        waitMs = min<uint32_t>(30000, waitMs * (1 + status.failures));
      }
      xSemaphoreGive(lock);
      if (ok) {
        std::vector<Aircraft> snap;
        flightsCopy(snap);
        checkAlerts(cfg, snap);
        noteSeen(snap);
      }
      uint32_t spent = millis() - t0;
      waitMs = waitMs > spent ? waitMs - spent : 200;
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(max<uint32_t>(waitMs, 500)));
  }
}

void flightsStart() {
  lock = xSemaphoreCreateMutex();
  status.activeSource = cfg.source;
  xTaskCreatePinnedToCore(fetchTask, "fetch", 12 * 1024, nullptr, 3, &task, 0);
}

void flightsKick() {
  const Settings cfg = cfgSnapshot();
  xSemaphoreTake(lock, portMAX_DELAY);
  status.activeSource = cfg.source;
  status.failures = 0;
  xSemaphoreGive(lock);
  if (task) xTaskNotifyGive(task);
}

void flightsRefilter() {
  const Settings cfg = cfgSnapshot();
  xSemaphoreTake(lock, portMAX_DELAY);
  status.shown = 0;
  for (auto &a : current) {
    geoDistBearing(cfg.lat, cfg.lon, a.lat, a.lon, a.distNm, a.bearing);
    a.visible = passesFilter(a, cfg);
    status.shown += a.visible;
  }
  version++;
  xSemaphoreGive(lock);
}

// ---------- "seen today" counter: distinct aircraft (by ICAO hex) that passed
// the filters, reset at local midnight
static uint32_t *seen = nullptr;
static const int SEEN_CAP = 4096;   // open-addressing hash set
static int seenCount = 0, seenDay = -1;

static void noteSeen(const std::vector<Aircraft> &list) {
  struct tm tm;
  int day = getLocalTime(&tm, 0) ? tm.tm_yday : seenDay;
  if (!seen) seen = (uint32_t *)heap_caps_calloc(SEEN_CAP, 4, MALLOC_CAP_SPIRAM);
  if (!seen) return;
  if (day != seenDay) { memset(seen, 0, SEEN_CAP * 4); seenCount = 0; seenDay = day; }
  for (const Aircraft &a : list) {
    if (!a.visible || seenCount > SEEN_CAP * 3 / 4) continue;
    uint32_t k = strtoul(a.hex, nullptr, 16) + 1;   // never 0
    uint32_t i = (k * 2654435761u) % SEEN_CAP;
    while (seen[i] && seen[i] != k) i = (i + 1) % SEEN_CAP;
    if (!seen[i]) { seen[i] = k; seenCount++; }
  }
}

int flightsSeenToday() { return seenCount; }

void flightsCopy(std::vector<Aircraft> &out) {
  xSemaphoreTake(lock, portMAX_DELAY);
  out = current;
  xSemaphoreGive(lock);
}

FlightStatus flightsStatus() {
  xSemaphoreTake(lock, portMAX_DELAY);
  FlightStatus s = status;
  xSemaphoreGive(lock);
  return s;
}

uint32_t flightsVersion() { return version; }
