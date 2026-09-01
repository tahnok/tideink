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

// True while a USB host is on the other end of the cable. Driven by the
// USB Serial/JTAG start-of-frame packets, so it means "enumerated by a host",
// not merely "something is supplying 5 V".
bool usbAttached();

// Stays awake for `seconds` instead of sleeping, keeping the USB Serial/JTAG
// peripheral enumerated so the host can reset the chip into its bootloader.
// Never waits longer than `capSeconds` in one go -- the caller restarts the
// cycle after that rather than spending a whole day in the loop.
// Returns the seconds left over, which is 0 when the window elapsed (or was cut
// short by the cap) and non-zero when the cable came out early.
int64_t stayAwakeFor(int64_t seconds, int64_t capSeconds);

// Sleeps for `seconds`, waking on the timer (and on the button, if enabled).
// This is the deepest sleep the board can hold together: everything the chip can
// power down is powered down, and what stays up is listed in power.cpp.
// Does not return.
void deepSleepFor(int64_t seconds);
