// Parsers for responses from the Canadian Hydrographic Service IWLS API
// (https://api-iwls.dfo-mpo.gc.ca). Both the firmware and the simulator use
// these, so a fixture captured from the live API exercises the same code the
// device runs.
//
// Responses look like:
//   [{"eventDate":"2026-08-08T03:06:00Z","qcFlagCode":"1","reviewed":false,
//     "timeSeriesId":"...","value":1.074}, ...]
#pragma once

#include <stddef.h>

#include "tide_data.h"

// Parses a `wlp-hilo` (high/low tide predictions) response into data.extremes
// and classifies each event. Returns false on malformed JSON.
bool iwlsParseHiLo(const char* json, size_t len, TideData& data);

// Parses a `wlp` (water level predictions) response into data.curve. The series
// is decimated if it is longer than kMaxCurvePoints. Returns false on malformed
// JSON or a non-uniform series.
bool iwlsParseCurve(const char* json, size_t len, TideData& data);
