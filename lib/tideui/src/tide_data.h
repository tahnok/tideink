// The tide dataset the clock keeps in memory (and in RTC memory across deep
// sleep), plus the small amount of logic that derives "what happens next" from
// it. Heights are stored in millimetres so the whole struct stays compact.
#pragma once

#include <stdint.h>

// A week of a semi-diurnal station, which sees four extremes a day; the mostly
// diurnal stations get twice the headroom.
static const uint8_t kMaxExtremes = 32;
// 48 h at 60-minute resolution, with headroom for finer resolutions.
static const uint16_t kMaxCurvePoints = 208;

static const uint8_t kMaxStationName = 32;

struct TideExtreme {
    int64_t time;      // Unix epoch seconds, UTC
    int16_t heightMm;  // metres above chart datum, in millimetres
    bool high;         // true = high tide, false = low tide
};

// A uniformly sampled water level prediction series.
struct TideCurve {
    int64_t startTime;   // epoch seconds of sample 0
    uint16_t stepSec;    // spacing between samples
    uint16_t count;      // samples in use
    int16_t heightMm[kMaxCurvePoints];
};

struct TideData {
    char stationName[kMaxStationName];
    TideExtreme extremes[kMaxExtremes];
    uint8_t extremeCount;
    TideCurve curve;
    int64_t fetchedAt;  // epoch seconds of the last successful download
    bool valid;
};

void tideDataReset(TideData& data);

// Appends an extreme, keeping the array sorted by time. Ignores duplicates and
// silently drops anything past kMaxExtremes.
bool tideDataAddExtreme(TideData& data, int64_t time, int16_t heightMm);

// Classifies each stored extreme as a high or a low by comparing it with its
// neighbours; call once after all extremes have been added. The IWLS wlp-hilo
// series alternates but does not label the events.
void tideDataClassifyExtremes(TideData& data);

// Next extreme at or after `now`, or nullptr. `high` selects the type.
const TideExtreme* tideNextExtreme(const TideData& data, int64_t now, bool high);
// Next extreme of either type, or nullptr.
const TideExtreme* tideNextAny(const TideData& data, int64_t now);
// Most recent extreme at or before `now`, or nullptr.
const TideExtreme* tidePreviousAny(const TideData& data, int64_t now);

// Interpolated water level at `t`, in millimetres. Returns false when `t` falls
// outside the stored curve.
bool tideHeightAt(const TideData& data, int64_t t, int16_t& outMm);

// Min/max of the curve over [from, to]. Returns false when the window is empty.
bool tideCurveRange(const TideData& data, int64_t from, int64_t to, int16_t& minMm,
                    int16_t& maxMm);

// True when the water is rising at `t` (derived from the surrounding extremes).
bool tideIsRising(const TideData& data, int64_t t);
