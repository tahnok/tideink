// Copy to secrets.h (which is gitignored) and fill in your network.
//
// config.h includes this file before it defines anything, so a #define here
// also overrides any default in config.h. That is the place to keep settings
// you would rather not commit -- the station you actually point the clock at,
// for instance.
#pragma once

#define WIFI_SSID "your-network"
#define WIFI_PASSWORD "your-password"

// Optional station override. IDs come from
// https://api-iwls.dfo-mpo.gc.ca/api/v1/stations
// #define IWLS_STATION_ID "5cebf1e33d0f4a073c4bc21f"
// #define STATION_DISPLAY_NAME "CHARLOTTETOWN, PE"
// #define STATION_NOTE "CHS station 01700  46.23N 63.12W"
// #define LOCAL_TZ "AST4ADT,M3.2.0,M11.1.0"
