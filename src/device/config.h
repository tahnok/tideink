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
// One download a day is the whole point of the battery budget; the screen is
// redrawn from the cached predictions in between.
#define DATA_REFRESH_HOURS 24
// Requested window, relative to the moment of the download. It has to cover the
// tide day on screen for every redraw until the next download. A download can
// land a minute before the kDayStartHour boundary, which puts the day already
// being drawn 24 h behind it, and the last redraw before the next download comes
// DATA_REFRESH_HOURS later still, which puts that day's end 24 h ahead:
//   graph start = fetch - 24 h,  graph end = fetch + DATA_REFRESH_HOURS + 24 h
// The extra couple of hours either side is slack for a late refresh.
#define CURVE_HOURS_BEFORE 26
#define CURVE_HOURS_AFTER 50
#define HILO_HOURS_BEFORE 28
#define HILO_HOURS_AFTER 52
// Valid values: ONE_MINUTE, THREE_MINUTES, FIVE_MINUTES, FIFTEEN_MINUTES,
// SIXTY_MINUTES. Hourly samples are ~10 kB for three days and plot smoothly.
#define CURVE_RESOLUTION "SIXTY_MINUTES"

// --------------------------------------------------------------- schedule ---
// Redraw at least this often so the countdown stays honest, and at most this
// often so the panel is not refreshed needlessly.
#define MIN_SLEEP_MINUTES 15
#define MAX_SLEEP_MINUTES 360
// Extra delay after a tide so the screen shows the *next* event, not the one
// that just happened.
#define WAKE_AFTER_TIDE_SECONDS 90
// Same idea for the tide day itself: wake just past kDayStartHour rather than
// exactly on it, so the new day is unambiguously the current one.
#define DAY_ROLLOVER_SECONDS 60
// Floor for a wake that is aimed at a tide rather than at the schedule. It only
// has to be long enough that sleeping is worth the boot, since MIN_SLEEP_MINUTES
// would otherwise overshoot the very event the wake exists to catch.
#define MIN_TIDE_SLEEP_SECONDS 60

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

// Stay awake instead of deep sleeping while a USB host is attached, so the
// USB Serial/JTAG port stays enumerated and `pio run -t upload` can reset the
// chip into its bootloader on its own. Costs nothing on a cable; set to 0 if
// you would rather the clock behave identically plugged in and unplugged.
#define STAY_AWAKE_ON_USB 1

// LiPo open-circuit voltage range used for the percentage readout.
#define BATTERY_EMPTY_MV 3300
#define BATTERY_FULL_MV 4150
