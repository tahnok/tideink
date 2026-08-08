#include "iwls_parse.h"

#include <ArduinoJson.h>

#include "time_utils.h"

namespace {

int16_t metresToMm(float v) {
    const float mm = v * 1000.0f;
    if (mm > 32767.0f) return 32767;
    if (mm < -32768.0f) return -32768;
    return (int16_t)(mm >= 0 ? mm + 0.5f : mm - 0.5f);
}

}  // namespace

bool iwlsParseHiLo(const char* json, size_t len, TideData& data) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull()) return false;

    data.extremeCount = 0;
    for (JsonObjectConst item : arr) {
        const char* date = item["eventDate"];
        if (!date || !item["value"].is<float>()) continue;
        const int64_t t = tiParseIso8601(date);
        if (t < 0) continue;
        if (!tideDataAddExtreme(data, t, metresToMm(item["value"].as<float>()))) break;
    }
    tideDataClassifyExtremes(data);
    return data.extremeCount > 0;
}

bool iwlsParseCurve(const char* json, size_t len, TideData& data) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull()) return false;

    const size_t total = arr.size();
    if (total < 2) return false;
    // Keep every Nth sample so a fine-resolution request still fits in the
    // fixed-size cache that survives deep sleep.
    const size_t decimate = (total + kMaxCurvePoints - 1) / kMaxCurvePoints;

    TideCurve& curve = data.curve;
    curve.count = 0;
    curve.startTime = 0;
    int64_t firstTime = -1;
    int64_t secondTime = -1;
    size_t index = 0;

    for (JsonObjectConst item : arr) {
        const bool keep = (index % decimate) == 0;
        index++;
        if (!keep) continue;

        const char* date = item["eventDate"];
        if (!date || !item["value"].is<float>()) continue;
        const int64_t t = tiParseIso8601(date);
        if (t < 0) continue;

        if (firstTime < 0) {
            firstTime = t;
            curve.startTime = t;
        } else if (secondTime < 0) {
            secondTime = t;
        }
        if (curve.count >= kMaxCurvePoints) break;
        curve.heightMm[curve.count++] = metresToMm(item["value"].as<float>());
    }

    if (curve.count < 2 || secondTime < 0) return false;
    const int64_t step = secondTime - firstTime;
    if (step <= 0 || step > 6 * 3600) return false;
    curve.stepSec = (uint16_t)step;
    return true;
}
