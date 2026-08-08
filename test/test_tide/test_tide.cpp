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

void test_countdown_formatting() {
    char buf[32];
    tiFormatCountdown(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("now", buf);
    tiFormatCountdown(40 * 60, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("in 40 min", buf);
    tiFormatCountdown(4 * 3600 + 12 * 60, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("in 4h 12m", buf);
    tiFormatCountdown(5 * 3600, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("in 5h", buf);
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

void test_rising_matches_extremes() {
    const TideData data = load();
    for (uint8_t i = 0; i + 1 < data.extremeCount; i++) {
        const int64_t midway = (data.extremes[i].time + data.extremes[i + 1].time) / 2;
        TEST_ASSERT_EQUAL_MESSAGE(data.extremes[i + 1].high, tideIsRising(data, midway),
                                  "water rises towards a high and falls towards a low");
    }
}

void test_curve_range_window() {
    const TideData data = load();
    int16_t lo = 0, hi = 0;
    const int64_t end = data.curve.startTime + (int64_t)(data.curve.count - 1) * data.curve.stepSec;
    TEST_ASSERT_TRUE(tideCurveRange(data, data.curve.startTime, end, lo, hi));
    TEST_ASSERT_LESS_THAN(hi, lo);
    TEST_ASSERT_FALSE(tideCurveRange(data, end + 3600, end + 7200, lo, hi));
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
    RUN_TEST(test_countdown_formatting);
    RUN_TEST(test_fixture_parses);
    RUN_TEST(test_extremes_alternate);
    RUN_TEST(test_next_high_and_low);
    RUN_TEST(test_height_interpolation);
    RUN_TEST(test_rising_matches_extremes);
    RUN_TEST(test_curve_range_window);
    RUN_TEST(test_rejects_garbage);
    return UNITY_END();
}
