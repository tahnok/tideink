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
void tiFormatClock(int64_t epoch, bool hour24, char* out, size_t n);  // "6:41 pm" / "18:41"
void tiFormatClockParts(int64_t epoch, bool hour24, char* time, size_t timeN, char* suffix,
                        size_t suffixN);                              // "6:41" + "PM"
void tiFormatShortDay(int64_t epoch, char* out, size_t n);            // "Sat"
void tiFormatDayDate(int64_t epoch, char* out, size_t n);             // "Sat 8 Aug"
// The hour a tide day starts, on its own: "6 am" / "06:00".
void tiFormatHourOfDay(int hour, bool hour24, char* out, size_t n);

// `hour` o'clock local time on the day that contains `epoch`. Goes through
// mktime(), so the result is a real local hour even on the two days a year
// daylight saving makes 23 or 25 hours long.
int64_t tiLocalTimeOfDay(int64_t epoch, int hour);

// The screen shows one "tide day" running from `hour` o'clock to `hour` o'clock
// rather than midnight to midnight. These bracket the one containing `epoch`;
// pass the start back to tiEndOfTideDay() for its end.
int64_t tiStartOfTideDay(int64_t epoch, int hour);
int64_t tiEndOfTideDay(int64_t start, int hour);

// Local hour-of-day (0-23) for `epoch`.
int tiLocalHour(int64_t epoch);
