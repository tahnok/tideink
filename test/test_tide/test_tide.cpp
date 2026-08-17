// Host tests for the tide model and the IWLS parser.
//
//   pio test -e sim
//
// The fixtures are unmodified responses captured from the live API by
// tools/fetch_fixtures.py, so these tests cover exactly what the device parses.

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <string>

#include "iwls_parse.h"
#include "moon.h"
#include "render.h"
#include "tide_data.h"
#include "time_utils.h"

namespace {

std::string readFixture(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

TideData load() {
    TideData data;
    tideDataReset(data);
    const std::string hilo = readFixture("test/fixtures/charlottetown_hilo.json");
    const std::string wlp = readFixture("test/fixtures/charlottetown_wlp.json");
    TEST_ASSERT_TRUE_MESSAGE(iwlsParseHiLo(hilo.c_str(), hilo.size(), data), "hilo fixture");
    TEST_ASSERT_TRUE_MESSAGE(iwlsParseCurve(wlp.c_str(), wlp.size(), data), "wlp fixture");
    return data;
}

}  // namespace

void test_parse_iso8601() {
    TEST_ASSERT_EQUAL_INT64(1754622000, tiParseIso8601("2025-08-08T03:00:00Z"));
    TEST_ASSERT_EQUAL_INT64(0, tiParseIso8601("1970-01-01T00:00:00Z"));
    TEST_ASSERT_EQUAL_INT64(-1, tiParseIso8601("not a date"));
}

void test_fixture_parses() {
    const TideData data = load();
    TEST_ASSERT_GREATER_THAN(4, data.extremeCount);
    TEST_ASSERT_GREATER_THAN(48, data.curve.count);
    TEST_ASSERT_EQUAL_UINT16(3600, data.curve.stepSec);
}

void test_extremes_alternate() {
    const TideData data = load();
    for (uint8_t i = 1; i < data.extremeCount; i++) {
        TEST_ASSERT_TRUE_MESSAGE(data.extremes[i].time > data.extremes[i - 1].time, "sorted");
        TEST_ASSERT_NOT_EQUAL_MESSAGE(data.extremes[i - 1].high, data.extremes[i].high,
                                      "highs and lows alternate");
    }
    // A high really is higher than the low next to it.
    for (uint8_t i = 1; i < data.extremeCount; i++) {
        if (data.extremes[i].high) {
            TEST_ASSERT_GREATER_THAN(data.extremes[i - 1].heightMm, data.extremes[i].heightMm);
        } else {
            TEST_ASSERT_LESS_THAN(data.extremes[i - 1].heightMm, data.extremes[i].heightMm);
        }
    }
}

void test_next_high_and_low() {
    const TideData data = load();
    const int64_t now = data.curve.startTime + 6 * 3600;

    const TideExtreme* high = tideNextExtreme(data, now, true);
    const TideExtreme* low = tideNextExtreme(data, now, false);
    TEST_ASSERT_NOT_NULL(high);
    TEST_ASSERT_NOT_NULL(low);
    TEST_ASSERT_TRUE(high->high);
    TEST_ASSERT_FALSE(low->high);
    TEST_ASSERT_GREATER_OR_EQUAL_INT64(now, high->time);
    TEST_ASSERT_GREATER_OR_EQUAL_INT64(now, low->time);

    // Whichever comes first must be the next event of any kind.
    const TideExtreme* next = tideNextAny(data, now);
    TEST_ASSERT_NOT_NULL(next);
    TEST_ASSERT_EQUAL_INT64(high->time < low->time ? high->time : low->time, next->time);
}

void test_height_interpolation() {
    const TideData data = load();
    int16_t mm = 0;

    TEST_ASSERT_TRUE(tideHeightAt(data, data.curve.startTime, mm));
    TEST_ASSERT_EQUAL_INT16(data.curve.heightMm[0], mm);

    // Halfway between two samples is halfway between their heights.
    TEST_ASSERT_TRUE(tideHeightAt(data, data.curve.startTime + 1800, mm));
    const int16_t expected = (int16_t)((data.curve.heightMm[0] + data.curve.heightMm[1]) / 2);
    TEST_ASSERT_INT16_WITHIN(2, expected, mm);

    // Outside the series there is nothing to interpolate.
    TEST_ASSERT_FALSE(tideHeightAt(data, data.curve.startTime - 1, mm));
    const int64_t end = data.curve.startTime + (int64_t)(data.curve.count - 1) * data.curve.stepSec;
    TEST_ASSERT_FALSE(tideHeightAt(data, end + 1, mm));
}

void test_curve_range_window() {
    const TideData data = load();
    int16_t lo = 0, hi = 0;
    const int64_t end = data.curve.startTime + (int64_t)(data.curve.count - 1) * data.curve.stepSec;
    TEST_ASSERT_TRUE(tideCurveRange(data, data.curve.startTime, end, lo, hi));
    TEST_ASSERT_LESS_THAN(hi, lo);
    TEST_ASSERT_FALSE(tideCurveRange(data, end + 3600, end + 7200, lo, hi));
}

void test_tide_day_window() {
    // 2026-08-08T12:00:00Z is 09:00 Atlantic, so the day on screen runs from
    // 06:00 that morning to 06:00 the next one.
    const int64_t nowMorning = tiParseIso8601("2026-08-08T12:00:00Z");
    const int64_t start = tiStartOfTideDay(nowMorning, kDayStartHour);
    const int64_t end = tiEndOfTideDay(start, kDayStartHour);
    TEST_ASSERT_EQUAL_INT64(tiParseIso8601("2026-08-08T09:00:00Z"), start);
    TEST_ASSERT_EQUAL_INT64(tiParseIso8601("2026-08-09T09:00:00Z"), end);
    TEST_ASSERT_EQUAL_INT(kDayStartHour, tiLocalHour(start));
    TEST_ASSERT_EQUAL_INT(kDayStartHour, tiLocalHour(end));

    // Two in the morning still belongs to the previous day's window.
    const int64_t nowSmallHours = tiParseIso8601("2026-08-09T05:00:00Z");
    TEST_ASSERT_EQUAL_INT64(start, tiStartOfTideDay(nowSmallHours, kDayStartHour));
    TEST_ASSERT_TRUE(nowSmallHours < end);

    // The boundary itself opens the new day rather than closing the old one.
    TEST_ASSERT_EQUAL_INT64(end, tiStartOfTideDay(end, kDayStartHour));
    TEST_ASSERT_EQUAL_INT64(start, tiStartOfTideDay(end - 1, kDayStartHour));
}

void test_tide_day_spans_daylight_saving() {
    // Atlantic springs forward on 2026-03-08 and falls back on 2026-11-01, both
    // at 2 am -- which lands in the window opened at 6 am the day before, making
    // it 23 or 25 hours long. A fixed 86400 would drift the boundary by an hour.
    const int64_t spring = tiStartOfTideDay(tiParseIso8601("2026-03-07T15:00:00Z"), kDayStartHour);
    TEST_ASSERT_EQUAL_INT(kDayStartHour, tiLocalHour(spring));
    TEST_ASSERT_EQUAL_INT(kDayStartHour, tiLocalHour(tiEndOfTideDay(spring, kDayStartHour)));
    TEST_ASSERT_EQUAL_INT64(23 * 3600, tiEndOfTideDay(spring, kDayStartHour) - spring);

    const int64_t fall = tiStartOfTideDay(tiParseIso8601("2026-10-31T15:00:00Z"), kDayStartHour);
    TEST_ASSERT_EQUAL_INT(kDayStartHour, tiLocalHour(fall));
    TEST_ASSERT_EQUAL_INT(kDayStartHour, tiLocalHour(tiEndOfTideDay(fall, kDayStartHour)));
    TEST_ASSERT_EQUAL_INT64(25 * 3600, tiEndOfTideDay(fall, kDayStartHour) - fall);
}

void test_extremes_in_window() {
    const TideData data = load();
    const int64_t start = tiStartOfTideDay(tiParseIso8601("2026-08-09T20:00:00Z"), kDayStartHour);
    const int64_t end = tiEndOfTideDay(start, kDayStartHour);

    const TideExtreme* shown[8];
    const uint8_t n = tideExtremesIn(data, start, end, shown, 8);
    // Charlottetown gets its usual four on 9 August.
    TEST_ASSERT_EQUAL_UINT8(4, n);
    for (uint8_t i = 0; i < n; i++) {
        TEST_ASSERT_GREATER_OR_EQUAL_INT64(start, shown[i]->time);
        TEST_ASSERT_LESS_THAN_INT64(end, shown[i]->time);
        if (i) TEST_ASSERT_GREATER_THAN_INT64(shown[i - 1]->time, shown[i]->time);
    }

    // The cap is honoured even when the window holds more.
    TEST_ASSERT_EQUAL_UINT8(2, tideExtremesIn(data, start, end, shown, 2));
    // An empty window yields nothing.
    TEST_ASSERT_EQUAL_UINT8(0, tideExtremesIn(data, start, start, shown, 8));

    // The day before drops to three: five extremes span about 24.8 hours, so a
    // 24-hour window cannot hold more than four and sometimes holds one fewer.
    const int64_t prevStart = tiStartOfTideDay(start - 1, kDayStartHour);
    TEST_ASSERT_EQUAL_UINT8(3, tideExtremesIn(data, prevStart, start, shown, 8));
}

void test_moon_phase() {
    // The reference new moon itself, and the quarters that follow it.
    const int64_t newMoon = 947182440;
    const int64_t quarter = (int64_t)(29.530588853 * 86400 / 4);

    const MoonPhase n = moonPhaseAt(newMoon);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, n.illumination);
    TEST_ASSERT_EQUAL_STRING("New moon", n.name);

    const MoonPhase first = moonPhaseAt(newMoon + quarter);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.5f, first.illumination);
    TEST_ASSERT_TRUE(first.waxing);
    TEST_ASSERT_EQUAL_STRING("First quarter", first.name);

    const MoonPhase full = moonPhaseAt(newMoon + 2 * quarter);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, full.illumination);
    TEST_ASSERT_EQUAL_STRING("Full moon", full.name);

    const MoonPhase last = moonPhaseAt(newMoon + 3 * quarter);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.5f, last.illumination);
    TEST_ASSERT_FALSE(last.waxing);
    TEST_ASSERT_EQUAL_STRING("Last quarter", last.name);

    // A lunation later everything has come back round.
    const MoonPhase again = moonPhaseAt(newMoon + 4 * quarter + 2551443);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, again.illumination);

    // Dates well before the reference epoch stay in range rather than going
    // negative through fmod().
    const MoonPhase old = moonPhaseAt(0);
    TEST_ASSERT_TRUE(old.fraction >= 0.0f && old.fraction < 1.0f);
    TEST_ASSERT_TRUE(old.illumination >= 0.0f && old.illumination <= 1.0f);

    // The full moon of 2026-08-28 04:18 UTC, to within the couple of hours the
    // mean phase is expected to differ from the true one.
    const MoonPhase august = moonPhaseAt(tiParseIso8601("2026-08-28T04:18:00Z"));
    TEST_ASSERT_EQUAL_STRING("Full moon", august.name);
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 1.0f, august.illumination);
}

void test_rejects_garbage() {
    TideData data;
    tideDataReset(data);
    const char* junk = "{\"error\":\"nope\"}";
    TEST_ASSERT_FALSE(iwlsParseHiLo(junk, strlen(junk), data));
    TEST_ASSERT_FALSE(iwlsParseCurve(junk, strlen(junk), data));
}

int main(int, char**) {
    tiSetTimezone("AST4ADT,M3.2.0,M11.1.0");
    UNITY_BEGIN();
    RUN_TEST(test_parse_iso8601);
    RUN_TEST(test_fixture_parses);
    RUN_TEST(test_extremes_alternate);
    RUN_TEST(test_next_high_and_low);
    RUN_TEST(test_height_interpolation);
    RUN_TEST(test_curve_range_window);
    RUN_TEST(test_tide_day_window);
    RUN_TEST(test_tide_day_spans_daylight_saving);
    RUN_TEST(test_extremes_in_window);
    RUN_TEST(test_moon_phase);
    RUN_TEST(test_rejects_garbage);
    return UNITY_END();
}
