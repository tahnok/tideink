// Battery measurement and deep sleep scheduling.
#pragma once

#include <stdint.h>

void powerBegin();

// Battery voltage at the cell, in millivolts, or 0 when unavailable.
uint16_t batteryMillivolts();
// 0-100 from the LiPo curve in config.h, or -1 when unavailable.
int16_t batteryPercent();
bool batteryCharging();

// True when the cell has fallen below BATTERY_CRITICAL_MV. Confirmed with a
// second reading BATTERY_CRITICAL_CONFIRM_MS later, because a cell sags under
// load and recovers -- one low sample is a busy radio, not a flat battery.
// `mv`, when given, receives the confirming reading, so the number that goes on
// screen is the one the decision was actually made on.
bool batteryCritical(uint16_t* mv = nullptr);

// True when this boot came from the front panel button rather than the timer.
bool wokeFromButton();

// True while a USB host is on the other end of the cable. Driven by the
// USB Serial/JTAG start-of-frame packets, so it means "enumerated by a host",
// not merely "something is supplying 5 V".
bool usbAttached();

// Stays awake for `seconds` instead of sleeping, keeping the USB Serial/JTAG
// peripheral enumerated so the host can reset the chip into its bootloader.
// Returns the seconds left over, which is 0 when the whole window elapsed and
// non-zero when the cable came out early.
int64_t stayAwakeFor(int64_t seconds);

// Sleeps for `seconds`, waking on the timer (and on the button, if enabled).
// Does not return.
void deepSleepFor(int64_t seconds);

// Switches the board off by releasing the battery latch, cutting the cell from
// the system rail. Whatever is on the panel stays there -- e-paper needs no
// power to hold an image -- so call this with the charge-me screen already
// shown. Does not return.
//
// On battery that is the end of it: the rail collapses within about a second
// and no further code runs. With a cable attached the rail is held up from
// outside and the latch cannot win, so this falls back to deep sleep with the
// latch left released, which both finishes the job the moment the cable comes
// out and comes back in LOW_BATTERY_RECHECK_MINUTES to see whether the cell has
// taken a charge.
void powerOff();
