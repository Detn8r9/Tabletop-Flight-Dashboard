// Persistent settings for FlightRadar.
// Stored as one JSON blob in NVS ("Preferences"), so adding a field only
// needs a default here plus a line in toJson/fromJson.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define FW_VERSION "1.3.0"
#define HOSTNAME_DEFAULT "flightradar"
#define AP_SSID_PREFIX "FlightRadar-Setup"

// Aircraft classes shown in the "Categories" filter submenu.
enum AcClass : uint8_t {
  CLS_AIRLINER = 0,   // ADS-B emitter categories A3, A4, A5
  CLS_REGIONAL,       // A2 (small jets, turboprops)
  CLS_LIGHT,          // A1 (light GA)
  CLS_HIGHPERF,       // A6 (fast / high-g)
  CLS_HELI,           // A7 (rotorcraft)
  CLS_GLIDER,         // B1, B4 (gliders, ultralights)
  CLS_BALLOON,        // B2 (lighter than air)
  CLS_DRONE,          // B6 (UAV)
  CLS_MILITARY,       // database flag "military" (overrides the above)
  CLS_UNKNOWN,        // no category broadcast
  CLS_COUNT
};
extern const char *const AC_CLASS_KEYS[CLS_COUNT];

// What can appear next to each aircraft. The first three are on by default.
enum LabelField : uint8_t {
  LF_FLIGHT = 0,  // "Delta 1234"
  LF_MODEL,       // "Boeing 737"
  LF_ALT,         // "35,000 ft"
  LF_SPEED,       // "520 mph"
  LF_DIR,         // "flying NE"
  LF_DIST,        // "3.2 mi away"
  LF_VS,          // climbing / descending arrow
  LF_CODE,        // raw callsign "DAL1234"
  LF_REG,         // registration "N123DL"
  LF_TYPE,        // type code "B738"
  LF_SQUAWK,      // transponder code
  LF_ROUTE,       // usual route "MCI>DTW"
  LF_COUNT
};
extern const char *const LABEL_FIELD_KEYS[LF_COUNT];

enum ModelMode : uint8_t { MODEL_OFF = 0, MODEL_ONLY, MODEL_HIDE };
enum NightMode : uint8_t { NIGHT_OFF = 0, NIGHT_CALM, NIGHT_BLANK };
enum MapStyle  : uint8_t { MAP_STREETS = 0, MAP_DETAILED, MAP_BLANK };
enum Units     : uint8_t { UNITS_US = 0, UNITS_AVIATION, UNITS_METRIC };
enum Layout    : uint8_t { LAYOUT_PANEL = 0, LAYOUT_FULL };
// adsb.lol first: airplanes.live now requires registration (HTTP 403)
enum DataSrc   : uint8_t { SRC_ADSBLOL = 0, SRC_ADSBFI, SRC_AIRPLANESLIVE, SRC_COUNT };

struct Settings {
  // --- location ---
  bool   locSet = false;          // false until the user (or IP lookup) sets it
  bool   locApprox = false;       // true when it came from IP geolocation
  double lat = 0, lon = 0;
  String locName;                 // free text, e.g. "Home"

  // --- view ---
  float  rangeNm = 13;            // radius to the range ring ("zoom"), ~15 miles
  float  sweepSec = 4;            // seconds per radar revolution
  float  refreshSec = 2;          // data refresh interval
  uint8_t layout = LAYOUT_PANEL;
  uint8_t mapStyle = MAP_STREETS;
  uint8_t units = UNITS_US;        // ft + mph + miles
  bool   rings = true;
  bool   placeNames = true;      // town names on the map
  bool   labels = true;
  bool   leaders = true;          // projected-track line in front of each plane
  bool   labelFields[LF_COUNT] = {true, true, true, false, false, false, false, false, false, false, false, true};
  bool   smooth = true;           // planes glide between updates
  bool   trails = true;           // dotted track behind each plane
  uint8_t screenMode = 0;         // 0 radar, 1 airport board
  String airport;                 // IATA code for airport mode; empty = nearest

  // --- filters ---
  bool   classOn[CLS_COUNT] = {true, true, true, true, true, true, true, true, true, true};
  uint8_t modelMode = MODEL_OFF;
  String models;                  // comma list of ICAO type codes, '*' wildcard ok (B73*)
  bool   altFilter = false; int altMin = 0; int altMax = 60000;
  bool   hideGround = true;
  bool   callFilter = false; String callPrefixes;   // e.g. "DAL,UAL,SWA"
  bool   emergAlways = true;      // 7500/7600/7700 bypass every filter

  // --- alerts ---
  bool   alertsOn = false;
  bool   alertSound = true;
  uint8_t alertVolume = 70;
  bool   alertEmerg = true, alertMil = true, alertHeli = false, alertTypes = false;
  String alertTypeList;
  bool   alertNear = false; float alertNearNm = 3;

  // --- overhead spotlight / highlights ---
  bool   overheadOn = true;       // full-screen card when a plane passes close
  float  overheadNm = 1.5f;
  bool   overheadChime = true;
  bool   routesOn = true;         // look up where flights are going
  bool   highlightsCard = true;   // show "today's highlights" now and then

  // --- night mode ---
  uint8_t nightMode = NIGHT_OFF;
  uint8_t nightStart = 22, nightEnd = 7;

  // --- system ---
  String tzName = "America/New_York";
  String tzPosix = "EST5EDT,M3.2.0,M11.1.0";
  String hostname = HOSTNAME_DEFAULT;
  uint8_t source = SRC_ADSBLOL;
  bool   autoUpdate = true;
  String updateRepo;              // "owner/repo" on GitHub with firmware.bin releases

  // --- hardware ---
  bool   showClimate = true;      // room temperature & humidity in the panel
};

extern Settings cfg;

// cfg is written by the web server task and read by the render/fetch tasks.
// Writers hold the lock; long-running readers work on a snapshot.
void cfgLock();
void cfgUnlock();
Settings cfgSnapshot();

void configLoad();
void configSave();
void configToJson(const Settings &s, JsonObject o);
// Applies JSON on top of s. Returns bitmask of what changed (CHG_*).
uint32_t configFromJson(Settings &s, JsonObjectConst o);
enum : uint32_t { CHG_MAP = 1, CHG_DATA = 2, CHG_TZ = 4, CHG_HOST = 8 };

// Wi-Fi credentials are kept separately so the web UI never echoes them.
bool wifiCredsLoad(String &ssid, String &pass);
void wifiCredsSave(const String &ssid, const String &pass);

void factoryReset();   // wipes settings, Wi-Fi, map cache; caller reboots
