// Build-time configuration for the tide clock.
//
// Wi-Fi credentials live in secrets.h (copy secrets.example.h and edit); this
// file holds everything else. secrets.h is included first, so anything defined
// there wins over the defaults below.
#pragma once

#include "secrets.h"

// ---------------------------------------------------------------- station ---
// Station IDs come from https://api-iwls.dfo-mpo.gc.ca/api/v1/stations
// 5cebf1e33d0f4a073c4bc21f = Charlottetown, PE (CHS station 01700).
#ifndef IWLS_STATION_ID
#define IWLS_STATION_ID "5cebf1e33d0f4a073c4bc21f"
#endif
#ifndef STATION_DISPLAY_NAME
#define STATION_DISPLAY_NAME "CHARLOTTETOWN, PE"
#endif

// POSIX TZ string. Atlantic time (AST/ADT) as observed on Prince Edward Island.
#ifndef LOCAL_TZ
#define LOCAL_TZ "AST4ADT,M3.2.0,M11.1.0"
#endif
// Set to true for a 24-hour clock.
#define CLOCK_24H false

// ------------------------------------------------------------------- data ---
// One download a day is the whole point of the battery budget. The download is
// pinned to the tide-day boundary rather than to "24 h since the last one" --
// see refreshDue() in main.cpp -- so this is only the backstop that catches a
// clock which somehow woke inside the day it already fetched in.
#define DATA_REFRESH_HOURS 24
// Requested window, relative to the moment of the download. It has to cover the
// tide day on screen for every draw the cache is asked to serve. A download can
// land either side of the kDayStartHour boundary, which puts the day being drawn
// up to 24 h behind it, and the cache has to keep serving until the next
// download DATA_REFRESH_HOURS later, whose day ends 24 h beyond that:
//   graph start = fetch - 24 h,  graph end = fetch + DATA_REFRESH_HOURS + 24 h
// The extra couple of hours either side is slack for a late refresh. It is also
// what lets the clock ride out a day of Wi-Fi outage: yesterday's download still
// covers the whole of today's tide day, so the screen stays right while the
// hourly retry keeps trying.
#define CURVE_HOURS_BEFORE 26
#define CURVE_HOURS_AFTER 50
#define HILO_HOURS_BEFORE 28
#define HILO_HOURS_AFTER 52
// Valid values: ONE_MINUTE, THREE_MINUTES, FIVE_MINUTES, FIFTEEN_MINUTES,
// SIXTY_MINUTES. Hourly samples are ~10 kB for three days and plot smoothly.
#define CURVE_RESOLUTION "SIXTY_MINUTES"

// --------------------------------------------------------------- schedule ---
// One wake a day is the battery budget. The screen shows a tide day that runs
// from kDayStartHour to kDayStartHour, so that boundary is the only moment the
// drawing genuinely has to change: waking there downloads the new day and draws
// it in the same breath, for one radio session and one full-panel refresh every
// 24 hours. Everything else the clock could wake for is a nicety.
//
// The nicety it gives up: everything on screen that tracks the clock rather
// than the calendar -- the cursor on the graph, the "now N.N m above chart
// datum" readout, the bar marking the next tide due -- is only true at the
// moment of the draw, and by evening it is pointing at the morning. That is the
// trade, and it is deliberate.

// Wake just past kDayStartHour rather than exactly on it, so the new day is
// unambiguously the current one.
#define DAY_ROLLOVER_SECONDS 60

// Deep sleep is timed by the C3's internal RC oscillator -- this board has no
// 32.768 kHz crystal -- which lands within about 1% of the requested duration,
// and worse away from room temperature (docs/hardware.md). Over a 24 h sleep 1%
// is a quarter of an hour either way, so a sleep aimed squarely at 6 am ends
// before the boundary as often as after it.
//
// The clock cannot simply notice and try again, either: the RTC it would check
// against is the very thing that drifted, so time() reads 6 am on the nose
// whichever way the oscillator went, and only the NTP sync a few seconds into
// the refresh reveals that it is really 5:45. By then the radio session has
// already been spent.
//
// So every sleep is padded by this fraction of its own length, which buys back
// the drift and puts the wake on the late side of the moment it was aimed at.
// The screen therefore turns over a quarter of an hour or so after 6 am rather
// than on the dot -- the price of not paying for a second wake to correct the
// first. Raise it if the clock lives somewhere with a wide daily temperature
// swing; the drawnScreenId() net in main.cpp catches whatever it does not.
#define SLEEP_DRIFT_PERMILLE 10

// A refresh that failed has nothing new to put on the screen, so it does not
// wait for the next day boundary. Retry on this interval instead: often enough
// to catch a router that comes back, rare enough that a week-long outage costs a
// handful of radio sessions a day rather than a hundred.
#define RETRY_SLEEP_MINUTES 60

// Floor and ceiling for a scheduled sleep. Neither is a schedule; both are
// guards. The floor only has to be long enough that sleeping is worth the boot,
// for the case where a wake lands a few seconds short of the boundary it was
// aimed at. The ceiling catches a day boundary that comes back absurd -- a
// broken TZ, a nonsense clock -- because the cost of getting that wrong on a
// device in a window is a clock that sleeps for years.
#define MIN_SLEEP_SECONDS 60
#define MAX_SLEEP_MINUTES 1500

#define WIFI_TIMEOUT_MS 25000
#define NTP_TIMEOUT_MS 15000
#define HTTP_TIMEOUT_MS 20000
#define NTP_SERVER_1 "time.nrc.ca"
#define NTP_SERVER_2 "pool.ntp.org"

// ------------------------------------------------------------------- pins ---
// Xteink X4 (ESP32-C3). Display is a 4.26" 800x480 GDEQ0426T82 on an SSD1677.
// Every pin below is confirmed on hardware; docs/hardware.md has the readings,
// the six other buttons this firmware ignores, and what the board does not have
// (no external RTC, no fuel gauge, no 32.768 kHz crystal).
#define PIN_EPD_SCK 8
#define PIN_EPD_MOSI 10
#define PIN_EPD_CS 21
#define PIN_EPD_DC 4
#define PIN_EPD_RST 5
#define PIN_EPD_BUSY 6

// Battery sense: GPIO0 sits behind a divider on the cell.
#define PIN_BATTERY_ADC 0
#define BATTERY_DIVIDER 2.0f
// USB charge detect. GPIO20 is driven HIGH with a cable attached and reads LOW
// without one -- measured in both directions, see docs/hardware.md.
#define PIN_CHARGE_DETECT 20
#define CHARGE_ACTIVE_LEVEL HIGH
#define ENABLE_CHARGE_DETECT 1

// Battery latch MOSFET. GPIO13 gates the cell's path to the system rail: held
// HIGH the board runs off the battery, released (or driven LOW) it powers down
// the moment USB goes away. It has to stay asserted through deep sleep, so it is
// latched with gpio_hold_en() -- otherwise the pad floats as soon as the GPIO
// matrix powers down and the clock dies mid-sleep.
//
// CrossPoint drives this pin LOW deliberately, to power the reader off on
// battery when the user closes it. A tide clock wants the opposite.
#define PIN_BATTERY_LATCH 13
#define BATTERY_LATCH_ACTIVE_LEVEL HIGH
#define ENABLE_BATTERY_LATCH 1

// Front panel power button, used to force an immediate refresh.
#define PIN_WAKE_BUTTON 3
#define WAKE_BUTTON_ACTIVE_LOW 1
#define ENABLE_BUTTON_WAKE 1

// Front panel button ladder (GPIO1: BACK / CONFIRM / LEFT / RIGHT).
// RIGHT pulls GPIO1 to ground, making it the only button on this ladder that
// can trigger a deep-sleep GPIO wake on the C3 (the others stay above the
// digital LOW threshold). See docs/hardware.md for the full ladder.
#define PIN_BUTTON_ADC1 1
#define OTA_BUTTON_RAW 3
#define OTA_BUTTON_TOLERANCE 200
#define OTA_TIMEOUT_MS 300000
#define OTA_HOSTNAME "tideink"
#define ENABLE_OTA_WAKE 1

// Stay awake instead of deep sleeping while a USB host is attached, so the
// USB Serial/JTAG port stays enumerated and `pio run -t upload` can reset the
// chip into its bootloader on its own. Costs nothing on a cable; set to 0 if
// you would rather the clock behave identically plugged in and unplugged.
#define STAY_AWAKE_ON_USB 1
// Longest single stretch to spend awake on the cable before restarting into the
// next cycle. The schedule is a whole day long now, and the restart clears RTC
// memory along with the cached predictions (docs/hardware.md), so waiting a
// day out in one go would mean a plugged-in clock never refreshed its screen
// while an hour-long cap would re-download eight times a shift. Six hours is
// the compromise, and it is what the old six-hour sleep ceiling used to give.
#define USB_AWAKE_MAX_MINUTES 360

// The percentage readout comes from a rest-voltage curve, not a straight line
// between two anchors -- a LiPo's discharge is nowhere near linear. The table
// lives next to the code that reads it, in kLipoCurveMv in power.cpp.

// ------------------------------------------------------------ low battery ---
// Below BATTERY_CRITICAL_MV the clock stops being a clock: it draws the
// charge-me screen and releases the battery latch, which cuts the cell off the
// system rail and switches the board off outright. The e-paper keeps that image
// with no power behind it, so the dead device still explains itself.
//
// 3450 mV is the 0% end of the curve in power.cpp, so the clock switches off at
// the point it has been calling empty all along rather than at some lower number
// of its own. That anchor is where a LiPo falls off the cliff -- below it the
// remaining runtime is minutes -- and it leaves headroom for the sag under the
// panel refresh that draws the charge-me screen, which is the heaviest current
// the board ever pulls. Without that headroom the screen is left half-written.
#define ENABLE_LOW_BATTERY_SHUTDOWN 1
#define BATTERY_CRITICAL_MV 3450
// A cell sags under load and recovers, so one low sample is not a flat battery.
// The reading is taken again after this long and both have to agree.
#define BATTERY_CRITICAL_CONFIRM_MS 400
// Only reached when the latch could not switch the board off, i.e. USB is
// holding the rail up. Wake this often to see whether the cell has taken a
// charge; on battery nothing is running to wake.
#define LOW_BATTERY_RECHECK_MINUTES 30
