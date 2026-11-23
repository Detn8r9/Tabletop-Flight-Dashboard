// Automatic updates from GitHub releases.
// The repository's latest release must have a tag like "v1.3.1" and a
// "firmware.bin" asset (the GitHub Actions workflow in .github/ makes both).
#pragma once
#include <Arduino.h>

// Baked-in default for the update repository ("owner/repo"); can be changed
// on the settings page (System > Advanced).
#ifndef DEFAULT_UPDATE_REPO
#define DEFAULT_UPDATE_REPO "Detn8r9/Tabletop-Flight-Dashboard"
#endif

void updaterStart();
void updaterCheckNow(bool install);   // install = also install if newer
String updaterStatus();               // human-readable state for the web page
String updaterLatest();               // latest version seen on GitHub, "" if unknown
bool updaterBusy();                   // true while installing (screen shows a notice)
// Pure helper: true if version string a is newer than b ("1.10.0" > "1.9.2")
bool versionNewer(const char *a, const char *b);
