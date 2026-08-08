// Downloads tide predictions from the Canadian Hydrographic Service IWLS API.
#pragma once

#include "tide_data.h"

enum class FetchStatus {
    kOk,
    kWifiFailed,
    kClockFailed,
    kHttpFailed,
    kParseFailed,
};

const char* fetchStatusMessage(FetchStatus status);

// Brings up Wi-Fi, syncs the clock over NTP, downloads both prediction series
// and, on success, replaces `data` wholesale. `data` is left untouched on
// failure so a stale cache keeps being displayed. The radio is switched off
// again before returning either way.
FetchStatus iwlsRefresh(TideData& data);
