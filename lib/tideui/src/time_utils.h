// Date/time helpers shared by the firmware and the simulator.
//
// Everything internal is Unix epoch seconds in UTC; formatting goes through the
// C library's local time using the TZ string configured at startup, so the
// device follows Atlantic daylight saving without any extra rules.
#pragma once

#include <stdint.h>
#include <stddef.h>

// Sets the process/task timezone, e.g. "AST4ADT,M3.2.0,M11.1.0" for PEI.
void tiSetTimezone(const char* tz);

// Parses "2026-08-08T03:06:00Z" (and the fractional-second variant the IWLS API
// sometimes returns). Returns -1 if the string is not a timestamp.
int64_t tiParseIso8601(const char* s);

// Local-time formatting. Each writes a NUL-terminated string into `out`.
void tiFormatClock(int64_t epoch, bool hour24, char* out, size_t n);     // "6:41 pm" / "18:41"
void tiFormatClockParts(int64_t epoch, bool hour24, char* time, size_t timeN, char* suffix,
                        size_t suffixN);                                 // "6:41" + "PM"
void tiFormatShortDay(int64_t epoch, char* out, size_t n);               // "Sat"
void tiFormatDateTime(int64_t epoch, bool hour24, char* out, size_t n);  // "8 Aug 06:12"

// "4h ago", "12 min ago"
void tiFormatAge(int64_t seconds, char* out, size_t n);

// Local midnight at or before `epoch`.
int64_t tiStartOfLocalDay(int64_t epoch);
// Local hour-of-day (0-23) for `epoch`.
int tiLocalHour(int64_t epoch);
