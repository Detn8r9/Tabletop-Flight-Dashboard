// Street-map background: OpenStreetMap tiles converted to a clean
// black-and-white map (major roads, water, parks), cached in flash.
#pragma once
#include <Arduino.h>

#define MAP_W 400                 // the map covers the whole screen
#define MAP_H 300
#define MAP_ROW_BYTES ((MAP_W + 7) / 8)
#define MAP_BYTES (MAP_ROW_BYTES * MAP_H)
#define SCOPE_R 146               // px from center to the "range" ring

// Where the radar center sits: middle of the screen (full layout) or middle
// of the area left of the side panel.
static inline int scopeCx(uint8_t layout) { return layout == 1 ? 200 : 150; }
static inline int scopeCy(uint8_t) { return 150; }

// Map pixel classes, from an OSM tile pixel's colour
enum MapClass : uint8_t { MC_NONE = 0, MC_PARK, MC_WATER, MC_MINOR, MC_MID, MC_MAJOR, MC_MOTOR };
MapClass mapClassify(uint8_t r, uint8_t g, uint8_t b);

void mapStart();                  // background task; loads cache if valid
void mapRequest();                // rebuild for the current settings
bool mapReady();
uint32_t mapVersion();            // bumps when a new image is available
bool mapCopy(uint8_t *dst);       // MAP_BYTES, 1 = ink
String mapStatus();               // "ready", "downloading 3/9", "error: ..."

// Turns a MAP_W x MAP_H grid of classes into the 1-bit image.
// style: 0 = simple streets, 1 = detailed streets, 2 = none.
void mapRender(const uint8_t *cls, uint8_t style, uint8_t *bits);
