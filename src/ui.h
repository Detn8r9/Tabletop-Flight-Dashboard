// Everything drawn on the device screen.
#pragma once
#include <Arduino.h>

void uiInit();
// Draws one frame into gfx (does not push). Returns false when nothing
// changed and the push can be skipped (night "calm" mode).
bool uiFrame();
void uiShowInfo(uint32_t ms);            // IP / QR overlay for a while
void uiSetHoldProgress(int secondsLeft); // factory-reset countdown (-1 = none)
void uiMessage(const char *title, const char *line);   // full-screen notice
bool uiDismiss();                        // closes a full-screen card; true if one was open
void uiShowSpotlight();                  // "what's that plane?" card for the closest plane
void uiShowHighlights();                 // today's highlights card
int uiPlanesOnScreen();          // planes currently drawn on the radar
