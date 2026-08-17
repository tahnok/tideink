#include "moon.h"

#include <math.h>

namespace {

// New moon of 2000-01-06 18:14 UTC, the usual epoch for this calculation.
const int64_t kReferenceNewMoon = 947182440;
// Mean synodic month, 29.530588853 days.
const double kSynodicSeconds = 29.530588853 * 86400.0;

// The eight conventional phase names, each claiming an eighth of the cycle
// centred on its exact moment -- so "full moon" covers the day and a half either
// side of the instant, which is what anyone looking up would call it.
const char* phaseName(double fraction) {
    static const char* const kNames[] = {"New moon",     "Waxing crescent", "First quarter",
                                         "Waxing gibbous", "Full moon",     "Waning gibbous",
                                         "Last quarter",   "Waning crescent"};
    const int bin = (int)(fraction * 8.0 + 0.5) % 8;
    return kNames[bin];
}

}  // namespace

MoonPhase moonPhaseAt(int64_t epoch) {
    double cycles = (double)(epoch - kReferenceNewMoon) / kSynodicSeconds;
    double fraction = fmod(cycles, 1.0);
    if (fraction < 0) fraction += 1.0;

    MoonPhase m;
    m.fraction = (float)fraction;
    m.ageDays = (float)(fraction * 29.530588853);
    m.illumination = (float)((1.0 - cos(2.0 * M_PI * fraction)) / 2.0);
    m.waxing = fraction < 0.5;
    m.name = phaseName(fraction);
    return m;
}
