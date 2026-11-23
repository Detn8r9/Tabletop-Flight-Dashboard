// "Where is this flight going?" Looks up a callsign's usual route on
// adsbdb.com (free, no key) in the background and caches the answers.
#pragma once
#include <Arduino.h>

struct RouteInfo {
  char from[4] = "";        // IATA, e.g. "MCI"
  char to[4] = "";
  char fromCity[24] = "";   // "Kansas City"
  char toCity[24] = "";
};

void routesStart();
// Queue a callsign for lookup (nearest-first: call in order of distance).
void routesWant(const char *callsign);
// True when a route is known. Never blocks on the network.
bool routeFor(const char *callsign, RouteInfo &out);
int routesCached();
