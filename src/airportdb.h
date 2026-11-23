// Lookups in the embedded airport list (data/airports.bin).
#pragma once
#include <Arduino.h>
#include <math.h>

struct AirportRec {
  double lat, lon;
  uint8_t kind;        // 2 large, 1 medium
  char code[4];
  char city[25];
};

const uint8_t *airportsData(size_t &len);   // basemap.cpp

static inline bool airportAt(const uint8_t *d, size_t len, size_t &off, AirportRec &a) {
  if (off + 13 > len) return false;
  const uint8_t *r = d + off;
  auto rd = [](const uint8_t *p) { return (int32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); };
  a.lat = rd(r) / 1e5;
  a.lon = rd(r + 4) / 1e5;
  a.kind = r[8];
  memcpy(a.code, r + 9, 3);
  a.code[3] = 0;
  int n = min((int)r[12], 24);
  memcpy(a.city, r + 13, n);
  a.city[n] = 0;
  off += 13 + r[12];
  return true;
}

// By IATA code ("JFK"), or else the nearest large airport to lat/lon
// (a medium one if no large airport is within 60 nm).
static inline bool airportFind(const char *code, double lat, double lon, AirportRec &out) {
  size_t len, off = 0;
  const uint8_t *d = airportsData(len);
  AirportRec a, bestLarge, bestAny;
  double dLarge = 1e9, dAny = 1e9;
  const double k = cos(lat * 0.0174533);
  while (airportAt(d, len, off, a)) {
    if (code && code[0]) {
      if (!strcasecmp(a.code, code)) { out = a; return true; }
      continue;
    }
    double dy = a.lat - lat, dx = (a.lon - lon) * k;
    double dist = dx * dx + dy * dy;
    if (a.kind == 2 && dist < dLarge) { dLarge = dist; bestLarge = a; }
    if (dist < dAny) { dAny = dist; bestAny = a; }
  }
  if (code && code[0]) return false;
  // about 60 nm in degrees, squared
  if (dLarge < 1.0) { out = bestLarge; return true; }
  if (dAny < 1e8) { out = bestAny; return true; }
  return false;
}
