// Human-friendly text for aircraft data, in the user's chosen units.
#pragma once
#include "config.h"
#include "flights.h"
#include "names.h"

static inline const char *unitDist(uint8_t u) { return u == UNITS_METRIC ? "km" : (u == UNITS_AVIATION ? "nm" : "mi"); }
static inline float nmTo(uint8_t u, float nm) { return u == UNITS_METRIC ? nm * 1.852f : (u == UNITS_AVIATION ? nm : nm * 1.15078f); }
static inline float toNm(uint8_t u, float v) { return v / nmTo(u, 1.0f); }

// 35000 -> "35,000"
static inline void withCommas(char *out, size_t n, long v) {
  char tmp[24];
  snprintf(tmp, sizeof tmp, "%ld", v < 0 ? -v : v);
  int len = strlen(tmp), o = 0;
  if (v < 0 && o < (int)n - 1) out[o++] = '-';
  for (int i = 0; i < len && o < (int)n - 1; i++) {
    out[o++] = tmp[i];
    int rest = len - 1 - i;
    if (rest > 0 && rest % 3 == 0 && o < (int)n - 1) out[o++] = ',';
  }
  out[o] = 0;
}

static inline void fmtAltitude(char *b, size_t n, const Aircraft &a, uint8_t u) {
  if (a.ground) { snprintf(b, n, "on ground"); return; }
  if (!a.altValid) { snprintf(b, n, "alt unknown"); return; }
  char num[16];
  if (u == UNITS_METRIC) {
    withCommas(num, sizeof num, lroundf(a.alt * 0.3048f / 10) * 10);
    snprintf(b, n, "%s m", num);
  } else {
    withCommas(num, sizeof num, (a.alt + 50) / 100 * 100);
    snprintf(b, n, "%s ft", num);
  }
}

static inline void fmtSpeed(char *b, size_t n, const Aircraft &a, uint8_t u) {
  if (!a.gsValid) { b[0] = 0; return; }
  if (u == UNITS_METRIC) snprintf(b, n, "%d km/h", (int)lroundf(a.gs * 1.852f));
  else if (u == UNITS_AVIATION) snprintf(b, n, "%d kt", (int)lroundf(a.gs));
  else snprintf(b, n, "%d mph", (int)lroundf(a.gs * 1.15078f));
}

static inline const char *compass8(float deg) {
  static const char *d[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return d[((int)lroundf(deg / 45.0f)) & 7];
}

static inline void fmtDist(char *b, size_t n, float nm, uint8_t u) {
  float v = nmTo(u, nm);
  if (v < 10) snprintf(b, n, "%.1f %s", v, unitDist(u));
  else snprintf(b, n, "%d %s", (int)lroundf(v), unitDist(u));
}

// A word for the kind of aircraft, used when the exact model is unknown.
static inline const char *classWord(uint8_t cls) {
  switch (cls) {
    case CLS_AIRLINER: return "Airliner";
    case CLS_REGIONAL: return "Small jet";
    case CLS_LIGHT: return "Small plane";
    case CLS_HIGHPERF: return "Fast jet";
    case CLS_HELI: return "Helicopter";
    case CLS_GLIDER: return "Glider";
    case CLS_BALLOON: return "Balloon";
    case CLS_DRONE: return "Drone";
    case CLS_MILITARY: return "Military";
    default: return "Aircraft";
  }
}

static inline const char *modelName(const Aircraft &a) {
  const char *m = aircraftModel(a.type);
  return m ? m : classWord(a.cls);
}

// "Delta 1234", or the registration / hex code if there's no callsign
static inline void flightName(char *b, size_t n, const Aircraft &a) {
  if (a.call[0]) friendlyFlight(a.call, b, n);
  else snprintf(b, n, "%s", a.reg[0] ? a.reg : a.hex);
}
