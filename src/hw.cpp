#include "hw.h"
#include <Wire.h>
#include <sys/time.h>
#include <time.h>

#define RTC_ADDR 0x51       // PCF85063
#define SHTC3_ADDR 0x70
#define PIN_BATTERY 4       // ADC1 channel 3, battery through a 1/3 divider

static SemaphoreHandle_t i2cMutex = xSemaphoreCreateRecursiveMutex();
void i2cLock() { xSemaphoreTakeRecursive(i2cMutex, portMAX_DELAY); }
void i2cUnlock() { xSemaphoreGiveRecursive(i2cMutex); }

static bool rtcOk = false, shtcOk = false;
static float lastT = NAN, lastH = NAN;
static int batteryPct = -1;

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

// ---------------- clock chip

static bool rtcRead(struct tm &t, bool &valid) {
  i2cLock();
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x04);   // seconds register
  bool ok = Wire.endTransmission(false) == 0 && Wire.requestFrom(RTC_ADDR, 7) == 7;
  uint8_t r[7] = {0};
  for (int i = 0; ok && i < 7; i++) r[i] = Wire.read();
  i2cUnlock();
  if (!ok) return false;
  valid = !(r[0] & 0x80);   // "oscillator stopped" flag: time not trustworthy
  memset(&t, 0, sizeof t);
  t.tm_sec = bcd2bin(r[0] & 0x7F);
  t.tm_min = bcd2bin(r[1] & 0x7F);
  t.tm_hour = bcd2bin(r[2] & 0x3F);
  t.tm_mday = bcd2bin(r[3] & 0x3F);
  t.tm_mon = bcd2bin(r[5] & 0x1F) - 1;
  t.tm_year = bcd2bin(r[6]) + 100;   // 20xx
  return true;
}

void hwRtcSaveTime() {
  if (!rtcOk) return;
  time_t now = time(nullptr);
  if (now < 1700000000) return;   // clock not synced yet
  struct tm t;
  gmtime_r(&now, &t);             // the chip keeps UTC
  i2cLock();
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x04);
  Wire.write(bin2bcd(t.tm_sec));  // also clears the oscillator-stopped flag
  Wire.write(bin2bcd(t.tm_min));
  Wire.write(bin2bcd(t.tm_hour));
  Wire.write(bin2bcd(t.tm_mday));
  Wire.write(t.tm_wday);
  Wire.write(bin2bcd(t.tm_mon + 1));
  Wire.write(bin2bcd(t.tm_year % 100));
  Wire.endTransmission();
  i2cUnlock();
}

bool hwRtcPresent() { return rtcOk; }

// ---------------- temperature / humidity

static uint8_t crc8(const uint8_t *d, int n) {
  uint8_t c = 0xFF;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = c & 0x80 ? (c << 1) ^ 0x31 : c << 1;
  }
  return c;
}

static bool shtcCmd(uint16_t cmd) {
  Wire.beginTransmission(SHTC3_ADDR);
  Wire.write(cmd >> 8);
  Wire.write(cmd & 0xFF);
  return Wire.endTransmission() == 0;
}

static bool shtcMeasure(float &t, float &h) {
  i2cLock();
  bool ok = shtcCmd(0x3517);              // wake up
  delayMicroseconds(300);
  ok = ok && shtcCmd(0x7866);             // measure, temperature first, normal mode
  i2cUnlock();
  if (!ok) return false;
  delay(15);
  i2cLock();
  uint8_t r[6];
  ok = Wire.requestFrom(SHTC3_ADDR, 6) == 6;
  for (int i = 0; ok && i < 6; i++) r[i] = Wire.read();
  shtcCmd(0xB098);                        // back to sleep
  i2cUnlock();
  if (!ok || crc8(r, 2) != r[2] || crc8(r + 3, 2) != r[5]) return false;
  // Waveshare subtracts 4 C: the sensor sits next to warm electronics
  t = 175.0f * ((r[0] << 8) | r[1]) / 65536.0f - 45.0f - 4.0f;
  h = 100.0f * ((r[3] << 8) | r[4]) / 65536.0f;
  return true;
}

bool hwClimate(float &t, float &h) {
  if (!shtcOk || isnan(lastT)) return false;
  t = lastT;
  h = lastH;
  return true;
}

// ---------------- battery

static int readBatteryPercent() {
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BATTERY);
  float v = mv / 8.0f * 3.0f / 1000.0f;
  if (v < 2.5f) return -1;                  // no battery fitted
  float pct = (v - 3.3f) / (4.15f - 3.3f) * 100.0f;
  return (int)constrain(pct, 0.0f, 100.0f);
}

int hwBatteryPercent() { return batteryPct; }

// ---------------- background polling

static void hwTask(void *) {
  uint32_t lastRtcSave = 0;
  for (;;) {
    float t, h;
    if (shtcOk && shtcMeasure(t, h)) { lastT = t; lastH = h; }
    batteryPct = readBatteryPercent();
    // keep the clock chip in step with internet time, every 6 hours
    if (rtcOk && time(nullptr) > 1700000000 && (lastRtcSave == 0 || millis() - lastRtcSave > 6 * 3600000UL)) {
      hwRtcSaveTime();
      lastRtcSave = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(30000));
  }
}

void hwStart() {
  analogSetPinAttenuation(PIN_BATTERY, ADC_11db);
  // clock chip: if it holds a valid time, use it until the internet clock syncs
  struct tm t;
  bool valid = false;
  rtcOk = rtcRead(t, valid);
  if (rtcOk && valid && t.tm_year >= 124) {
    time_t secs = mktime(&t);   // TZ is still UTC at boot, so this reads UTC
    struct timeval tv = {secs, 0};
    settimeofday(&tv, nullptr);
  }
  i2cLock();
  shtcOk = shtcCmd(0x3517);
  if (shtcOk) shtcCmd(0xB098);
  i2cUnlock();
  xTaskCreatePinnedToCore(hwTask, "hw", 4096, nullptr, 1, nullptr, 0);
}
