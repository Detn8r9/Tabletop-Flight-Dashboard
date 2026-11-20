// ST7305 reflective LCD (400x300, 1-bit) on the Waveshare ESP32-S3-RLCD-4.2.
// Init sequence and pixel packing come from Waveshare's example driver
// (Apache-2.0, github.com/waveshareteam/ESP32-S3-RLCD-4.2).
#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>

#define SCREEN_W 400
#define SCREEN_H 300

// Drawing surface: 1 = ink (black), 0 = paper (white).
extern GFXcanvas1 *gfx;

void displayInit();
void displayPush();             // send gfx to the panel (blocks ~12 ms)
void displayPower(bool on);     // panel on/off (night "screen off" mode)
// Copies the last pushed frame (row-major, MSB first, 1 = ink) into dst,
// which must hold SCREEN_W*SCREEN_H/8 bytes. Used by the web preview.
void displaySnapshot(uint8_t *dst);
