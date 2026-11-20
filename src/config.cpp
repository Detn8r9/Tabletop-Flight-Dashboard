#include "config.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <nvs_flash.h>

Settings cfg;

static SemaphoreHandle_t cfgMutex = xSemaphoreCreateRecursiveMutex();
void cfgLock() { xSemaphoreTakeRecursive(cfgMutex, portMAX_DELAY); }
void cfgUnlock() { xSemaphoreGiveRecursive(cfgMutex); }
Settings cfgSnapshot() {
  cfgLock();
  Settings s = cfg;
  cfgUnlock();
  return s;
}

const char *const AC_CLASS_KEYS[CLS_COUNT] = {
  "airliner", "regional", "light", "highperf", "heli",
  "glider", "balloon", "drone", "military", "unknown"};

const char *const LABEL_FIELD_KEYS[LF_COUNT] = {
  "flight", "model", "alt", "speed", "dir", "dist", "vs", "code", "reg", "type", "squawk"};

static Preferences prefs;

void configToJson(const Settings &s, JsonObject o) {
  JsonObject loc = o["location"].to<JsonObject>();
  loc["set"] = s.locSet;
  loc["approx"] = s.locApprox;
  loc["lat"] = s.lat;
  loc["lon"] = s.lon;
  loc["name"] = s.locName;

  JsonObject v = o["view"].to<JsonObject>();
  v["range"] = s.rangeNm;
  v["sweep"] = s.sweepSec;
  v["refresh"] = s.refreshSec;
  v["layout"] = s.layout;
  v["map"] = s.mapStyle;
  v["rings"] = s.rings;
  v["labels"] = s.labels;
  v["leaders"] = s.leaders;
  v["units"] = s.units;
  JsonObject lf = v["show"].to<JsonObject>();
  for (int i = 0; i < LF_COUNT; i++) lf[LABEL_FIELD_KEYS[i]] = s.labelFields[i];

  JsonObject f = o["filter"].to<JsonObject>();
  JsonObject cl = f["classes"].to<JsonObject>();
  for (int i = 0; i < CLS_COUNT; i++) cl[AC_CLASS_KEYS[i]] = s.classOn[i];
  f["modelMode"] = s.modelMode;
  f["models"] = s.models;
  f["altOn"] = s.altFilter;
  f["altMin"] = s.altMin;
  f["altMax"] = s.altMax;
  f["hideGround"] = s.hideGround;
  f["callOn"] = s.callFilter;
  f["calls"] = s.callPrefixes;
  f["emergAlways"] = s.emergAlways;

  JsonObject a = o["alerts"].to<JsonObject>();
  a["on"] = s.alertsOn;
  a["sound"] = s.alertSound;
  a["volume"] = s.alertVolume;
  a["emerg"] = s.alertEmerg;
  a["mil"] = s.alertMil;
  a["heli"] = s.alertHeli;
  a["typesOn"] = s.alertTypes;
  a["types"] = s.alertTypeList;
  a["nearOn"] = s.alertNear;
  a["nearNm"] = s.alertNearNm;

  JsonObject n = o["night"].to<JsonObject>();
  n["mode"] = s.nightMode;
  n["start"] = s.nightStart;
  n["end"] = s.nightEnd;

  JsonObject y = o["system"].to<JsonObject>();
  y["tz"] = s.tzName;
  y["tzPosix"] = s.tzPosix;
  y["hostname"] = s.hostname;
  y["source"] = s.source;
}

template <typename T>
static bool setIf(JsonVariantConst v, T &dst) {
  if (v.isNull() || !v.is<T>()) return false;
  T nv = v.as<T>();
  if (nv == dst) return false;
  dst = nv;
  return true;
}
static bool setStr(JsonVariantConst v, String &dst, size_t maxLen = 400) {
  if (v.isNull()) return false;
  String nv = v.as<String>();
  nv.trim();
  if (nv.length() > maxLen) nv = nv.substring(0, maxLen);
  if (nv == dst) return false;
  dst = nv;
  return true;
}
// JSON numbers may arrive as int or float; accept either.
static bool setNum(JsonVariantConst v, float &dst, float lo, float hi) {
  if (v.isNull() || !(v.is<float>() || v.is<int>())) return false;
  float nv = constrain(v.as<float>(), lo, hi);
  if (fabsf(nv - dst) < 1e-6f) return false;
  dst = nv;
  return true;
}
static bool setNum(JsonVariantConst v, double &dst, double lo, double hi) {
  if (v.isNull() || !(v.is<double>() || v.is<int>())) return false;
  double nv = constrain(v.as<double>(), lo, hi);
  if (fabs(nv - dst) < 1e-9) return false;
  dst = nv;
  return true;
}
static bool setInt(JsonVariantConst v, int &dst, int lo, int hi) {
  if (v.isNull() || !(v.is<int>() || v.is<float>())) return false;
  int nv = constrain((int)lroundf(v.as<float>()), lo, hi);
  if (nv == dst) return false;
  dst = nv;
  return true;
}
static bool setU8(JsonVariantConst v, uint8_t &dst, int lo, int hi) {
  int t = dst;
  bool c = setInt(v, t, lo, hi);
  dst = (uint8_t)t;
  return c;
}

uint32_t configFromJson(Settings &s, JsonObjectConst o) {
  uint32_t chg = 0;
  JsonObjectConst loc = o["location"];
  if (!loc.isNull()) {
    bool c = false;
    c |= setNum(loc["lat"], s.lat, -85.0, 85.0);
    c |= setNum(loc["lon"], s.lon, -180.0, 180.0);
    setStr(loc["name"], s.locName, 40);
    if (!loc["approx"].isNull()) s.locApprox = loc["approx"].as<bool>();
    if (c || !loc["lat"].isNull()) {
      if (!s.locSet) c = true;
      s.locSet = true;
    }
    if (c) chg |= CHG_MAP | CHG_DATA;
  }
  JsonObjectConst v = o["view"];
  if (!v.isNull()) {
    if (setNum(v["range"], s.rangeNm, 2.0f, 250.0f)) chg |= CHG_MAP | CHG_DATA;
    setNum(v["sweep"], s.sweepSec, 1.5f, 12.0f);
    if (setNum(v["refresh"], s.refreshSec, 2.0f, 60.0f)) chg |= CHG_DATA;
    if (setU8(v["layout"], s.layout, 0, 1)) chg |= CHG_MAP;
    if (setU8(v["map"], s.mapStyle, 0, 2)) chg |= CHG_MAP;
    setIf(v["rings"], s.rings);
    setIf(v["labels"], s.labels);
    setIf(v["leaders"], s.leaders);
    setU8(v["units"], s.units, 0, 2);
    JsonObjectConst lf = v["show"];
    for (int i = 0; i < LF_COUNT && !lf.isNull(); i++) setIf(lf[LABEL_FIELD_KEYS[i]], s.labelFields[i]);
  }
  JsonObjectConst f = o["filter"];
  if (!f.isNull()) {
    JsonObjectConst cl = f["classes"];
    for (int i = 0; i < CLS_COUNT && !cl.isNull(); i++) setIf(cl[AC_CLASS_KEYS[i]], s.classOn[i]);
    setU8(f["modelMode"], s.modelMode, 0, 2);
    if (setStr(f["models"], s.models, 600)) s.models.toUpperCase();
    setIf(f["altOn"], s.altFilter);
    setInt(f["altMin"], s.altMin, -2000, 70000);
    setInt(f["altMax"], s.altMax, -2000, 70000);
    setIf(f["hideGround"], s.hideGround);
    setIf(f["callOn"], s.callFilter);
    if (setStr(f["calls"], s.callPrefixes, 300)) s.callPrefixes.toUpperCase();
    setIf(f["emergAlways"], s.emergAlways);
  }
  JsonObjectConst a = o["alerts"];
  if (!a.isNull()) {
    setIf(a["on"], s.alertsOn);
    setIf(a["sound"], s.alertSound);
    setU8(a["volume"], s.alertVolume, 0, 100);
    setIf(a["emerg"], s.alertEmerg);
    setIf(a["mil"], s.alertMil);
    setIf(a["heli"], s.alertHeli);
    setIf(a["typesOn"], s.alertTypes);
    if (setStr(a["types"], s.alertTypeList, 300)) s.alertTypeList.toUpperCase();
    setIf(a["nearOn"], s.alertNear);
    setNum(a["nearNm"], s.alertNearNm, 0.5f, 50.0f);
  }
  JsonObjectConst n = o["night"];
  if (!n.isNull()) {
    setU8(n["mode"], s.nightMode, 0, 2);
    setU8(n["start"], s.nightStart, 0, 23);
    setU8(n["end"], s.nightEnd, 0, 23);
  }
  JsonObjectConst y = o["system"];
  if (!y.isNull()) {
    setStr(y["tz"], s.tzName, 60);
    if (setStr(y["tzPosix"], s.tzPosix, 60)) chg |= CHG_TZ;
    String h = s.hostname;
    if (setStr(y["hostname"], h, 30)) {
      // keep it a valid DNS label
      String clean;
      for (char ch : h) {
        ch = tolower(ch);
        if (isalnum(ch) || ch == '-') clean += ch;
      }
      if (clean.length() && clean != s.hostname) {
        s.hostname = clean;
        chg |= CHG_HOST;
      }
    }
    if (setU8(y["source"], s.source, 0, SRC_COUNT - 1)) chg |= CHG_DATA;
  }
  return chg;
}

void configLoad() {
  prefs.begin("fr", true);
  String blob = prefs.getString("cfg", "");
  prefs.end();
  if (!blob.length()) return;
  JsonDocument doc;
  if (deserializeJson(doc, blob)) return;
  configFromJson(cfg, doc.as<JsonObjectConst>());
  // locSet is derived from presence of lat in fromJson; restore exact flag
  cfg.locSet = doc["location"]["set"] | false;
}

void configSave() {
  JsonDocument doc;
  cfgLock();
  configToJson(cfg, doc.to<JsonObject>());
  cfgUnlock();
  String blob;
  serializeJson(doc, blob);
  prefs.begin("fr", false);
  prefs.putString("cfg", blob);
  prefs.end();
}

bool wifiCredsLoad(String &ssid, String &pass) {
  prefs.begin("wifi", true);
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  prefs.end();
  return ssid.length() > 0;
}

void wifiCredsSave(const String &ssid, const String &pass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

void factoryReset() {
  prefs.begin("fr", false);  prefs.clear(); prefs.end();
  prefs.begin("wifi", false); prefs.clear(); prefs.end();
  LittleFS.format();   // cached maps (they reveal the location)
}
