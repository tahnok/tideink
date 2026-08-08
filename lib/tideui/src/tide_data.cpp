#include "tide_data.h"

#include <string.h>

void tideDataReset(TideData& data) {
    memset(&data, 0, sizeof(data));
    data.curve.stepSec = 3600;
}

bool tideDataAddExtreme(TideData& data, int64_t time, int16_t heightMm) {
    if (data.extremeCount >= kMaxExtremes) return false;
    // Insertion sort: the API returns events in order, so this is normally O(1).
    uint8_t i = data.extremeCount;
    while (i > 0 && data.extremes[i - 1].time > time) {
        if (data.extremes[i - 1].time == time) return false;
        data.extremes[i] = data.extremes[i - 1];
        i--;
    }
    if (i > 0 && data.extremes[i - 1].time == time) return false;
    data.extremes[i].time = time;
    data.extremes[i].heightMm = heightMm;
    data.extremes[i].high = false;
    data.extremeCount++;
    return true;
}

void tideDataClassifyExtremes(TideData& data) {
    // The wlp-hilo series strictly alternates between highs and lows, so each
    // event only has to be compared with the neighbour that follows it (or the
    // one before it, for the final event).
    const uint8_t n = data.extremeCount;
    if (n == 0) return;
    if (n == 1) {
        data.extremes[0].high = true;
        return;
    }
    for (uint8_t i = 0; i + 1 < n; i++) {
        data.extremes[i].high = data.extremes[i].heightMm > data.extremes[i + 1].heightMm;
    }
    data.extremes[n - 1].high = data.extremes[n - 1].heightMm > data.extremes[n - 2].heightMm;
}

const TideExtreme* tideNextExtreme(const TideData& data, int64_t now, bool high) {
    for (uint8_t i = 0; i < data.extremeCount; i++) {
        if (data.extremes[i].time >= now && data.extremes[i].high == high) {
            return &data.extremes[i];
        }
    }
    return nullptr;
}

const TideExtreme* tideNextAny(const TideData& data, int64_t now) {
    for (uint8_t i = 0; i < data.extremeCount; i++) {
        if (data.extremes[i].time >= now) return &data.extremes[i];
    }
    return nullptr;
}

const TideExtreme* tidePreviousAny(const TideData& data, int64_t now) {
    const TideExtreme* best = nullptr;
    for (uint8_t i = 0; i < data.extremeCount; i++) {
        if (data.extremes[i].time <= now) best = &data.extremes[i];
    }
    return best;
}

bool tideHeightAt(const TideData& data, int64_t t, int16_t& outMm) {
    const TideCurve& c = data.curve;
    if (c.count == 0 || c.stepSec == 0) return false;
    const int64_t end = c.startTime + (int64_t)(c.count - 1) * c.stepSec;
    if (t < c.startTime || t > end) return false;

    const int64_t offset = t - c.startTime;
    const uint16_t i = (uint16_t)(offset / c.stepSec);
    if (i >= c.count - 1) {
        outMm = c.heightMm[c.count - 1];
        return true;
    }
    const int32_t frac = (int32_t)(offset % c.stepSec);
    const int32_t a = c.heightMm[i];
    const int32_t b = c.heightMm[i + 1];
    outMm = (int16_t)(a + (b - a) * frac / (int32_t)c.stepSec);
    return true;
}

bool tideCurveRange(const TideData& data, int64_t from, int64_t to, int16_t& minMm,
                    int16_t& maxMm) {
    const TideCurve& c = data.curve;
    bool any = false;
    for (uint16_t i = 0; i < c.count; i++) {
        const int64_t t = c.startTime + (int64_t)i * c.stepSec;
        if (t < from || t > to) continue;
        if (!any) {
            minMm = maxMm = c.heightMm[i];
            any = true;
        } else {
            if (c.heightMm[i] < minMm) minMm = c.heightMm[i];
            if (c.heightMm[i] > maxMm) maxMm = c.heightMm[i];
        }
    }
    return any;
}

bool tideIsRising(const TideData& data, int64_t t) {
    const TideExtreme* next = tideNextAny(data, t);
    if (next) return next->high;
    const TideExtreme* prev = tidePreviousAny(data, t);
    if (prev) return !prev->high;
    return false;
}
