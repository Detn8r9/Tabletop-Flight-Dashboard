// FlightRadar for the Waveshare ESP32-S3-RLCD-4.2
// A self-contained radar-style display of aircraft overhead, configured
// from a web page the device serves on the local network.
#include <Arduino.h>
#include <ESPmDNS.h>
#include "config.h"
#include "display.h"
#include "ui.h"
#include "net.h"
#include "web.h"
#include "flights.h"
#include "basemap.h"
#include "audio.h"
#include "routes.h"
#include "hw.h"
#include "updater.h"
#include "airportdb.h"
#include <WiFi.h>

#define PIN_KEY  18
#define PIN_BOOT 0

static volatile uint32_t rebootAt = 0;
static const float RANGE_STEPS[] = {3, 5, 10, 15, 25, 40, 60, 100, 150, 250};
static const int RANGE_COUNT = sizeof(RANGE_STEPS) / sizeof(RANGE_STEPS[0]);

void requestReboot(uint32_t delayMs) { rebootAt = millis() + delayMs; }

static volatile uint32_t renderBeat = 0;   // watchdog heartbeat from the render task

// Airport for airport mode: the one set in settings, else the nearest big
// one to home. Cached until the settings change.
bool airportTarget(double &lat, double &lon, char *code, char *city) {
  static String key;
  static AirportRec rec;
  static bool ok = false;
  Settings s = cfgSnapshot();
  String k = s.airport + "|" + String(s.lat, 3) + "|" + String(s.lon, 3);
  if (k != key) {
    key = k;
    ok = s.locSet && airportFind(s.airport.c_str(), s.lat, s.lon, rec);
    if (!ok && s.airport.length()) ok = s.locSet && airportFind("", s.lat, s.lon, rec);   // bad code: nearest
  }
  if (!ok) return false;
  lat = rec.lat;
  lon = rec.lon;
  if (code) strlcpy(code, rec.code, 4);
  if (city) strlcpy(city, rec.city, 24);
  return true;
}

static void toggleAirportMode() {
  cfgLock();
  cfg.screenMode = cfg.screenMode ? 0 : 1;
  cfgUnlock();
  configSave();
  flightsKick();
}

void onLocationChanged() {
  mapRequest();
  flightsKick();
  flightsRefilter();
}

void onSettingsChanged(uint32_t chg) {
  if (chg & CHG_MAP) mapRequest();
  if (chg & CHG_DATA) flightsKick();
  if (chg & CHG_TZ) netApplyTimezone();
  if (chg & CHG_HOST) requestReboot(1500);   // new .local name needs a restart
  flightsRefilter();
}

static void zoom(int dir) {
  cfgLock();
  int idx = 0;
  float best = 1e9;
  for (int i = 0; i < RANGE_COUNT; i++) {
    float d = fabsf(RANGE_STEPS[i] - cfg.rangeNm);
    if (d < best) { best = d; idx = i; }
  }
  idx = constrain(idx + dir, 0, RANGE_COUNT - 1);
  bool changed = cfg.rangeNm != RANGE_STEPS[idx];
  cfg.rangeNm = RANGE_STEPS[idx];
  cfgUnlock();
  if (changed) {
    configSave();
    onSettingsChanged(CHG_MAP | CHG_DATA);
  }
}

// ---- buttons (polled from the render loop)
struct Btn {
  uint8_t pin;
  bool down = false;
  uint32_t since = 0;
  bool longFired = false;
};
static Btn keyBtn{PIN_KEY}, bootBtn{PIN_BOOT};

static void pollButtons() {
  const uint32_t now = millis();

  bool k = digitalRead(PIN_KEY) == LOW;
  bool b = digitalRead(PIN_BOOT) == LOW;

  // both buttons together: radar <-> airport board
  static bool both = false;
  if (k && b && !both) {
    both = true;
    keyBtn.longFired = bootBtn.longFired = true;   // don't also zoom
    toggleAirportMode();
  }
  if (!k && !b) both = false;
  if (both) { keyBtn.down = k; bootBtn.down = b; return; }

  // KEY: click = zoom in, hold 2 s = info page, hold 10 s = factory reset
  if (k && !keyBtn.down) { keyBtn.down = true; keyBtn.since = now; keyBtn.longFired = false; }
  if (k && keyBtn.down) {
    uint32_t held = now - keyBtn.since;
    if (held > 2000 && !keyBtn.longFired) { keyBtn.longFired = true; uiShowInfo(20000); }
    if (held > 3000) {
      int left = 10 - (int)(held / 1000);
      uiSetHoldProgress(max(left, 0));
      if (held >= 10000) {
        uiSetHoldProgress(-1);
        uiMessage("Factory reset", "Erasing settings and Wi-Fi...");
        factoryReset();
        delay(800);
        ESP.restart();
      }
    }
  }
  if (!k && keyBtn.down) {
    keyBtn.down = false;
    uiSetHoldProgress(-1);
    if (now - keyBtn.since < 800 && !keyBtn.longFired && !uiDismiss()) zoom(-1);
  }

  // BOOT: click = zoom out, hold 1.5 s = labels on/off
  if (b && !bootBtn.down) { bootBtn.down = true; bootBtn.since = now; bootBtn.longFired = false; }
  if (b && bootBtn.down && !bootBtn.longFired && now - bootBtn.since > 1500) {
    bootBtn.longFired = true;
    cfgLock();
    cfg.labels = !cfg.labels;
    cfgUnlock();
    configSave();
  }
  if (!b && bootBtn.down) {
    bootBtn.down = false;
    if (!bootBtn.longFired && now - bootBtn.since < 800 && !uiDismiss()) zoom(+1);
  }
}

static void renderTask(void *) {
  for (;;) {
    const uint32_t t0 = millis();
    renderBeat = t0;
    pollButtons();
    bool drew = uiFrame();
    if (drew) displayPush();
    const uint32_t spent = millis() - t0;
    const uint32_t frameMs = drew ? 66 : 200;   // ~15 fps while animating
    vTaskDelay(pdMS_TO_TICKS(spent < frameMs ? frameMs - spent : 5));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_KEY, INPUT_PULLUP);
  pinMode(PIN_BOOT, INPUT_PULLUP);

  configLoad();
  displayInit();
  uiInit();
  uiMessage("FlightRadar", "Starting up...");

  audioInit();
  hwStart();      // clock chip, room sensor, battery (shares the audio I2C bus)
  netStart();
  mapStart();
  flightsStart();
  routesStart();
  updaterStart();
  webStart();

  xTaskCreatePinnedToCore(renderTask, "render", 10 * 1024, nullptr, 2, nullptr, 1);
}

// Restarts the radar if something has quietly stopped working.
static void watchdog() {
  const uint32_t now = millis();
  if (renderBeat && now - renderBeat > 30000) {           // screen loop froze
    log_e("watchdog: render task stalled");
    ESP.restart();
  }
  // no plane data for an hour although Wi-Fi is up: start fresh (at most hourly)
  FlightStatus fs = flightsStatus();
  if (WiFi.isConnected() && now > 3600000UL && fs.failures > 0 && (!fs.lastOkMs || now - fs.lastOkMs > 3600000UL)) {
    log_e("watchdog: no data for an hour");
    ESP.restart();
  }
}

void loop() {
  netLoop();
  static uint32_t lastWd = 0;
  if (millis() - lastWd > 5000) { lastWd = millis(); watchdog(); }
  if (rebootAt && millis() > rebootAt) {
    uiMessage("Restarting", "");
    delay(300);
    ESP.restart();
  }
  delay(5);
}
