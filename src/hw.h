// On-board extras: PCF85063 clock chip, SHTC3 temperature/humidity sensor,
// battery voltage. All share the I2C bus with the audio codec.
#pragma once
#include <Arduino.h>

void hwStart();                         // call after audioInit() (which starts Wire)
void i2cLock();                         // serialize I2C between tasks
void i2cUnlock();

bool hwClimate(float &tempC, float &humidity);   // latest reading, false if no sensor
int hwBatteryPercent();                 // -1 when no battery is fitted
bool hwRtcPresent();
void hwRtcSaveTime();                   // copy the (NTP-synced) system clock into the chip
