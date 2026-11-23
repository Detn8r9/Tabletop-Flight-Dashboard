#include "highlights.h"
#include "fmt.h"

static Highlights hl;
static SemaphoreHandle_t lock = xSemaphoreCreateMutex();

// how many of each model were seen today, to find the rarest
struct TypeCount { char type[5]; uint16_t n; char name[24]; char model[24]; uint8_t hour; };
static TypeCount types[160];
static int nTypes = 0;

static void fill(HlEntry &e, const Aircraft &a, float v, int hour) {
  e.set = true;
  flightName(e.name, sizeof e.name, a);
  strlcpy(e.model, modelName(a), sizeof e.model);
  e.value = v;
  e.hour = hour;
}

void hlReset() {
  xSemaphoreTake(lock, portMAX_DELAY);
  hl = Highlights();
  nTypes = 0;
  xSemaphoreGive(lock);
}

void hlNewAircraft(const Aircraft &a, int hour) {
  xSemaphoreTake(lock, portMAX_DELAY);
  hl.total++;
  if (hour >= 0 && hour < 24) hl.perHour[hour]++;
  if (a.cls == CLS_MILITARY) {
    hl.militaryCount++;
    fill(hl.military, a, a.altValid ? a.alt : 0, hour);
  }
  if (a.type[0]) {
    int i = 0;
    while (i < nTypes && strcmp(types[i].type, a.type)) i++;
    if (i == nTypes && nTypes < 160) {
      strlcpy(types[i].type, a.type, sizeof types[i].type);
      types[i].n = 0;
      flightName(types[i].name, sizeof types[i].name, a);
      strlcpy(types[i].model, modelName(a), sizeof types[i].model);
      types[i].hour = hour;
      nTypes++;
    }
    if (i < nTypes) types[i].n++;
  }
  xSemaphoreGive(lock);
}

void hlUpdate(const Aircraft &a, int hour) {
  xSemaphoreTake(lock, portMAX_DELAY);
  if (a.altValid && !a.ground && (!hl.highest.set || a.alt > hl.highest.value)) fill(hl.highest, a, a.alt, hour);
  if (a.gsValid && !a.ground && (!hl.fastest.set || a.gs > hl.fastest.value)) fill(hl.fastest, a, a.gs, hour);
  if (!a.ground && (!hl.closest.set || a.distNm < hl.closest.value)) fill(hl.closest, a, a.distNm, hour);
  xSemaphoreGive(lock);
}

Highlights hlGet() {
  xSemaphoreTake(lock, portMAX_DELAY);
  Highlights out = hl;
  // busiest hour
  int best = 0;
  for (int h = 0; h < 24; h++)
    if (out.perHour[h] > best) { best = out.perHour[h]; out.busiestHour = h; }
  // rarest: a model seen only once (or least often) today; prefer named models
  int pick = -1;
  for (int i = 0; i < nTypes; i++) {
    bool named = aircraftModel(types[i].type) != types[i].type;   // known to our table
    if (pick < 0 || types[i].n < types[pick].n ||
        (types[i].n == types[pick].n && named && aircraftModel(types[pick].type) == types[pick].type))
      pick = i;
  }
  if (pick >= 0 && nTypes > 3) {
    out.rarest.set = true;
    strlcpy(out.rarest.name, types[pick].name, sizeof out.rarest.name);
    strlcpy(out.rarest.model, types[pick].model, sizeof out.rarest.model);
    out.rarest.value = types[pick].n;
    out.rarest.hour = types[pick].hour;
  }
  xSemaphoreGive(lock);
  return out;
}
