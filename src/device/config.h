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
#ifndef STATION_NOTE
#define STATION_NOTE "CHS station 01700  46.23N 63.12W"
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
// graph for every redraw until the next download:
//   graph end = fetch + DATA_REFRESH_HOURS + kGraphHoursAfter
#define CURVE_HOURS_BEFORE 8
#define CURVE_HOURS_AFTER 68
#define HILO_HOURS_BEFORE 12
#define HILO_HOURS_AFTER 76
// Valid values: ONE_MINUTE, THREE_MINUTES, FIVE_MINUTES, FIFTEEN_MINUTES,
// SIXTY_MINUTES. Hourly samples are ~10 kB for three days and plot smoothly.
#define CURVE_RESOLUTION "SIXTY_MINUTES"

// ---------------------------------------------------------------- TLS ------
// How the API's certificate chain is verified:
//
//   IWLS_TLS_BUNDLE  the Mozilla root store that ships inside ESP-IDF (~200
//                    roots, already compiled into the framework; costs about
//                    64 kB of flash and survives the API rotating issuers)
//   IWLS_TLS_PINNED  only IWLS_ROOT_CA below is trusted. Tighter, but the
//                    clock stops working the day that root is retired
//   IWLS_TLS_NONE    encrypted but unauthenticated; last resort for debugging
#define IWLS_TLS_BUNDLE 0
#define IWLS_TLS_PINNED 1
#define IWLS_TLS_NONE 2

#define IWLS_TLS_MODE IWLS_TLS_BUNDLE

// Only used when IWLS_TLS_MODE is IWLS_TLS_PINNED. `sh tools/fetch_ca.sh`
// prints a PEM literal ready to paste in.
#define IWLS_ROOT_CA ""

// --------------------------------------------------------------- schedule ---
// Redraw at least this often so the countdown stays honest, and at most this
// often so the panel is not refreshed needlessly.
#define MIN_SLEEP_MINUTES 15
#define MAX_SLEEP_MINUTES 360
// Extra delay after a tide so the screen shows the *next* event, not the one
// that just happened.
#define WAKE_AFTER_TIDE_SECONDS 90

#define WIFI_TIMEOUT_MS 25000
#define NTP_TIMEOUT_MS 15000
#define HTTP_TIMEOUT_MS 20000
#define NTP_SERVER_1 "time.nrc.ca"
#define NTP_SERVER_2 "pool.ntp.org"

// ------------------------------------------------------------------- pins ---
// Xteink X4 (ESP32-C3). Display is a 4.26" 800x480 GDEQ0426T82 on an SSD1677.
#define PIN_EPD_SCK 8
#define PIN_EPD_MOSI 10
#define PIN_EPD_CS 21
#define PIN_EPD_DC 4
#define PIN_EPD_RST 5
#define PIN_EPD_BUSY 6

// Battery sense: GPIO0 sits behind a divider on the cell.
#define PIN_BATTERY_ADC 0
#define BATTERY_DIVIDER 2.0f
// USB charge detect. Flip the active level if your unit reads inverted.
#define PIN_CHARGE_DETECT 20
#define CHARGE_ACTIVE_LEVEL LOW
#define ENABLE_CHARGE_DETECT 1

// Front panel power button, used to force an immediate refresh.
#define PIN_WAKE_BUTTON 3
#define WAKE_BUTTON_ACTIVE_LOW 1
#define ENABLE_BUTTON_WAKE 1

// LiPo open-circuit voltage range used for the percentage readout.
#define BATTERY_EMPTY_MV 3300
#define BATTERY_FULL_MV 4150
