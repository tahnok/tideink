// Battery measurement and deep sleep scheduling.
#pragma once

#include <stdint.h>

void powerBegin();

// Battery voltage at the cell, in millivolts, or 0 when unavailable.
uint16_t batteryMillivolts();
// 0-100 from the LiPo curve in config.h, or -1 when unavailable.
int16_t batteryPercent();
bool batteryCharging();

// True when this boot came from the front panel button rather than the timer.
bool wokeFromButton();

// Sleeps for `seconds`, waking on the timer (and on the button, if enabled).
// Does not return.
void deepSleepFor(int64_t seconds);
