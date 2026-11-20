// Terrain background for the radar scope.
// Downloads free elevation tiles (AWS Open Data "Terrarium" PNGs), turns them
// into a 1-bit contour + hillshade image, and caches it in flash.
#pragma once
#include <Arduino.h>

#define MAP_W 400                 // map image covers the whole screen
#define MAP_H 300
#define MAP_ROW_BYTES ((MAP_W + 7) / 8)
#define MAP_BYTES (MAP_ROW_BYTES * MAP_H)
#define SCOPE_R 146               // px from center to the "range" ring

// Where the radar center sits for a layout: the middle of the screen, or the
// middle of the area left of the side panel.
static inline int scopeCx(uint8_t layout) { return layout == 1 ? 200 : 150; }
static inline int scopeCy(uint8_t) { return 150; }

void terrainStart();              // background task; loads cache if valid
void terrainRequest();            // rebuild for the current cfg (debounced)
bool terrainReady();
uint32_t terrainVersion();        // bumps when a new image is available
// Copies the 1-bit full-screen map (1 = ink) into dst (MAP_BYTES bytes).
bool terrainCopy(uint8_t *dst);
String terrainStatus();           // "ready", "downloading 3/9", "error: ..."

// Exposed for desktop testing: render a MAP_W x MAP_H elevation grid
// (meters, INT16_MIN = no data) into the 1-bit image.
void terrainRender(const int16_t *elev, float metersPerPx, uint8_t style, uint8_t *bits);
