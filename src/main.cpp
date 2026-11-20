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
#include "terrain.h"
#include "audio.h"

#define PIN_KEY  18
#define PIN_BOOT 0

static volatile uint32_t rebootAt = 0;
static const float RANGE_STEPS[] = {3, 5, 10, 15, 25, 40, 60, 100, 150, 250};
static const int RANGE_COUNT = sizeof(RANGE_STEPS) / sizeof(RANGE_STEPS[0]);

void requestReboot(uint32_t delayMs) { rebootAt = millis() + delayMs; }

void onLocationChanged() {
  terrainRequest();
  flightsKick();
  flightsRefilter();
}

void onSettingsChanged(uint32_t chg) {
  if (chg & CHG_MAP) terrainRequest();
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

  // KEY: click = zoom in, hold 2 s = info page, hold 10 s = factory reset
  bool k = digitalRead(PIN_KEY) == LOW;
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
    if (now - keyBtn.since < 800) zoom(-1);
  }

  // BOOT: click = zoom out, hold 1.5 s = labels on/off
  bool b = digitalRead(PIN_BOOT) == LOW;
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
    if (!bootBtn.longFired && now - bootBtn.since < 800) zoom(+1);
  }
}

static void renderTask(void *) {
  for (;;) {
    const uint32_t t0 = millis();
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
  netStart();
  terrainStart();
  flightsStart();
  webStart();

  xTaskCreatePinnedToCore(renderTask, "render", 10 * 1024, nullptr, 2, nullptr, 1);
}

void loop() {
  netLoop();
  if (rebootAt && millis() > rebootAt) {
    uiMessage("Restarting", "");
    delay(300);
    ESP.restart();
  }
  delay(5);
}
