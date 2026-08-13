#include "time_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {

const char* const kDayShort[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const kMonthShort[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Howard Hinnant's days_from_civil: calendar date -> days since 1970-01-01.
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

bool localParts(int64_t epoch, struct tm& out) {
    const time_t t = (time_t)epoch;
    return localtime_r(&t, &out) != nullptr;
}

int hour12(int hour) {
    const int h = hour % 12;
    return h == 0 ? 12 : h;
}

}  // namespace

void tiSetTimezone(const char* tz) {
    setenv("TZ", tz, 1);
    tzset();
}

int64_t tiParseIso8601(const char* s) {
    if (!s) return -1;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second) < 5) {
        return -1;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31) return -1;
    // The API always reports UTC ("...Z"); no offset handling is needed.
    return daysFromCivil(year, (unsigned)month, (unsigned)day) * 86400 + hour * 3600 +
           minute * 60 + second;
}

void tiFormatClock(int64_t epoch, bool hour24, char* out, size_t n) {
    struct tm tm;
    if (!localParts(epoch, tm)) {
        snprintf(out, n, "--:--");
        return;
    }
    if (hour24) {
        snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
    } else {
        snprintf(out, n, "%d:%02d %s", hour12(tm.tm_hour), tm.tm_min,
                 tm.tm_hour < 12 ? "am" : "pm");
    }
}

void tiFormatClockParts(int64_t epoch, bool hour24, char* time, size_t timeN, char* suffix,
                        size_t suffixN) {
    struct tm tm;
    if (!localParts(epoch, tm)) {
        snprintf(time, timeN, "--:--");
        if (suffixN) suffix[0] = '\0';
        return;
    }
    if (hour24) {
        snprintf(time, timeN, "%02d:%02d", tm.tm_hour, tm.tm_min);
        if (suffixN) suffix[0] = '\0';
    } else {
        snprintf(time, timeN, "%d:%02d", hour12(tm.tm_hour), tm.tm_min);
        snprintf(suffix, suffixN, "%s", tm.tm_hour < 12 ? "AM" : "PM");
    }
}

void tiFormatShortDay(int64_t epoch, char* out, size_t n) {
    struct tm tm;
    if (!localParts(epoch, tm)) {
        snprintf(out, n, "---");
        return;
    }
    snprintf(out, n, "%s", kDayShort[tm.tm_wday % 7]);
}

void tiFormatDateTime(int64_t epoch, bool hour24, char* out, size_t n) {
    struct tm tm;
    if (!localParts(epoch, tm)) {
        snprintf(out, n, "--");
        return;
    }
    if (hour24) {
        snprintf(out, n, "%d %s %02d:%02d", tm.tm_mday, kMonthShort[tm.tm_mon % 12], tm.tm_hour,
                 tm.tm_min);
    } else {
        snprintf(out, n, "%d %s %d:%02d %s", tm.tm_mday, kMonthShort[tm.tm_mon % 12],
                 hour12(tm.tm_hour), tm.tm_min, tm.tm_hour < 12 ? "am" : "pm");
    }
}

void tiFormatAge(int64_t seconds, char* out, size_t n) {
    if (seconds < 0) seconds = 0;
    const int64_t minutes = seconds / 60;
    if (minutes < 1) {
        snprintf(out, n, "just now");
    } else if (minutes < 60) {
        snprintf(out, n, "%lld min ago", (long long)minutes);
    } else if (minutes < 60 * 48) {
        snprintf(out, n, "%lldh ago", (long long)(minutes / 60));
    } else {
        snprintf(out, n, "%lld days ago", (long long)(minutes / (60 * 24)));
    }
}

int64_t tiStartOfLocalDay(int64_t epoch) {
    struct tm tm;
    if (!localParts(epoch, tm)) return epoch;
    return epoch - (tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
}

int tiLocalHour(int64_t epoch) {
    struct tm tm;
    if (!localParts(epoch, tm)) return 0;
    return tm.tm_hour;
}
