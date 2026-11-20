// Live aircraft data: fetching, classification and filtering.
#pragma once
#include <Arduino.h>
#include <vector>
#include "config.h"

// Farthest screen corner is ~2x the range-ring radius (side-panel layout).
#define RANGE_TO_CORNER 2.0f
#define MAX_FETCH_NM 100.0f   // largest radius requested from the data source

struct Aircraft {
  char hex[7] = "";
  char call[9] = "";
  char reg[10] = "";
  char type[5] = "";
  char cat[3] = "";
  char squawk[5] = "";
  int32_t alt = 0;        // feet, barometric
  bool altValid = false;
  bool ground = false;
  float gs = 0;           // knots
  bool gsValid = false;
  float track = 0;        // degrees true
  bool trackValid = false;
  int32_t vs = 0;         // feet per minute
  bool vsValid = false;
  double lat = 0, lon = 0;
  uint8_t dbFlags = 0;
  bool emergency = false;
  // derived
  uint8_t cls = CLS_UNKNOWN;
  float distNm = 0;
  float bearing = 0;      // from home, degrees true
  bool visible = false;   // passes the user's filters and is within range
};

struct FlightStatus {
  uint32_t lastOkMs = 0;      // millis() of last successful fetch (0 = never)
  uint32_t lastTryMs = 0;
  int total = 0;              // aircraft returned by the source
  int shown = 0;              // after filters
  int failures = 0;           // consecutive
  uint8_t activeSource = 0;
  String error;               // last error, empty when healthy
};

// Snapshot accessors (thread-safe copies).
void flightsCopy(std::vector<Aircraft> &out);
FlightStatus flightsStatus();
uint32_t flightsVersion();   // increments on every successful update
int flightsSeenToday();      // distinct aircraft shown since local midnight

void flightsStart();         // starts the background fetch task
void flightsKick();          // fetch now (e.g. after location change)
void flightsRefilter();      // re-apply filters to the current data

// Pure helpers (also used by tests)
uint8_t classify(const char *cat, uint8_t dbFlags);
bool passesFilter(const Aircraft &a, const Settings &s);
bool matchTypeList(const char *type, const String &list);
void geoDistBearing(double lat1, double lon1, double lat2, double lon2, float &nm, float &brg);
const char *sourceName(uint8_t src);

// Alerts produced by the fetcher for the display/sound.
struct AlertEvent { char text[48]; uint32_t ms; };
bool alertsPending(AlertEvent &out);   // pops the newest unseen alert
