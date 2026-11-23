// "Today's highlights": highest, fastest and closest plane, the rarest model,
// military flyovers and the busiest hour. Reset at local midnight.
#pragma once
#include <Arduino.h>
#include "flights.h"

struct HlEntry {
  bool set = false;
  char name[24] = "";    // "Delta 1234"
  char model[24] = "";   // "Boeing 737"
  float value = 0;       // feet / knots / nautical miles, by entry
  uint8_t hour = 0;      // local hour it happened
};

struct Highlights {
  HlEntry highest, fastest, closest, rarest, military;
  int militaryCount = 0;
  int total = 0;
  int perHour[24] = {0};
  int busiestHour = -1;
};

void hlReset();
void hlNewAircraft(const Aircraft &a, int hour);   // first sighting today
void hlUpdate(const Aircraft &a, int hour);        // every sighting
Highlights hlGet();
