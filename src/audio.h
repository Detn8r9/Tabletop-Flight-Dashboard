// Alert chime through the on-board ES8311 codec and speaker.
#pragma once
#include <Arduino.h>

void audioInit();
// kind: 1 = normal two-tone chime, 2 = urgent triple beep. volume 0..100.
// Non-blocking: queued to a background task.
void audioChime(uint8_t kind, uint8_t volume);
