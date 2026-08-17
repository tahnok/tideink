// Moon phase for the header panel.
//
// This is the mean synodic phase -- the Moon's position in an average lunation,
// not the true ephemeris one. The two drift apart by up to about half a day
// around the quarters, which is invisible in a shaded disc and only ever moves
// the printed phase name across a bin boundary a few hours early or late. That
// is the right trade for a clock that has to compute it on a sleepy C3.
#pragma once

#include <stdint.h>

struct MoonPhase {
    float fraction;      // position in the cycle: 0 = new, 0.5 = full
    float illumination;  // lit fraction of the disc, 0 at new, 1 at full
    float ageDays;       // days since the last new moon
    bool waxing;         // lit limb on the right (northern hemisphere)
    const char* name;    // "Waxing crescent", "Full moon", ...
};

MoonPhase moonPhaseAt(int64_t epoch);
