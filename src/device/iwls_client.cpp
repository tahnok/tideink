#include "iwls_client.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "config.h"
#include "iwls_parse.h"
#include "secrets.h"

namespace {

const char* kApiBase = "https://api-iwls.dfo-mpo.gc.ca/api/v1";

// The parsed download lands here first so a half-finished refresh cannot
// corrupt the cached predictions. Static rather than stack: it is ~1.2 kB.
TideData g_incoming;

void formatUtc(int64_t epoch, char* out, size_t n) {
    const time_t t = (time_t)epoch;
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

bool connectWifi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const uint32_t deadline = millis() + WIFI_TIMEOUT_MS;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
        delay(150);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[net] Wi-Fi connect failed");
        return false;
    }
    Serial.printf("[net] connected, %d dBm, ip %s\n", WiFi.RSSI(),
                  WiFi.localIP().toString().c_str());
    return true;
}

void stopWifi() {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
}

bool syncClock() {
    // The RTC keeps running through deep sleep, so this only really matters on
    // the first boot and after a power cut.
    configTzTime(LOCAL_TZ, NTP_SERVER_1, NTP_SERVER_2);
    const uint32_t deadline = millis() + NTP_TIMEOUT_MS;
    while (time(nullptr) < 1700000000 && millis() < deadline) {
        delay(200);
    }
    if (time(nullptr) < 1700000000) {
        Serial.println("[net] NTP sync failed");
        return false;
    }
    return true;
}

bool httpGet(const char* url, String& body) {
    WiFiClientSecure client;
    const char* ca = IWLS_ROOT_CA;
    if (ca[0]) {
        client.setCACert(ca);
    } else {
        // No pinned root: still encrypted, but unauthenticated. See config.h.
        client.setInsecure();
    }
    client.setTimeout(HTTP_TIMEOUT_MS / 1000);

    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setReuse(false);
    if (!http.begin(client, url)) {
        Serial.println("[http] begin failed");
        return false;
    }
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[http] %s -> %d\n", url, code);
        http.end();
        return false;
    }
    body = http.getString();
    http.end();
    Serial.printf("[http] %d bytes\n", body.length());
    return body.length() > 2;
}

bool fetchSeries(int64_t now, const char* series, int hoursBefore, int hoursAfter,
                 const char* resolution, String& body) {
    char from[32], to[32];
    formatUtc(now - (int64_t)hoursBefore * 3600, from, sizeof(from));
    formatUtc(now + (int64_t)hoursAfter * 3600, to, sizeof(to));

    char url[256];
    snprintf(url, sizeof(url), "%s/stations/%s/data?time-series-code=%s&from=%s&to=%s%s%s",
             kApiBase, IWLS_STATION_ID, series, from, to, resolution ? "&resolution=" : "",
             resolution ? resolution : "");
    return httpGet(url, body);
}

}  // namespace

const char* fetchStatusMessage(FetchStatus status) {
    switch (status) {
        case FetchStatus::kOk:
            return "";
        case FetchStatus::kWifiFailed:
            return "WI-FI UNREACHABLE - SHOWING CACHED PREDICTIONS";
        case FetchStatus::kClockFailed:
            return "CLOCK NOT SET - COULD NOT REACH A TIME SERVER";
        case FetchStatus::kHttpFailed:
            return "TIDE SERVICE UNREACHABLE - SHOWING CACHED PREDICTIONS";
        case FetchStatus::kParseFailed:
            return "UNEXPECTED RESPONSE FROM THE TIDE SERVICE";
    }
    return "";
}

FetchStatus iwlsRefresh(TideData& data) {
    if (!connectWifi()) {
        stopWifi();
        return FetchStatus::kWifiFailed;
    }
    if (!syncClock()) {
        stopWifi();
        return FetchStatus::kClockFailed;
    }

    const int64_t now = (int64_t)time(nullptr);
    tideDataReset(g_incoming);
    snprintf(g_incoming.stationName, sizeof(g_incoming.stationName), "%s", STATION_DISPLAY_NAME);

    FetchStatus status = FetchStatus::kOk;
    {
        String body;
        if (!fetchSeries(now, "wlp-hilo", HILO_HOURS_BEFORE, HILO_HOURS_AFTER, nullptr, body)) {
            status = FetchStatus::kHttpFailed;
        } else if (!iwlsParseHiLo(body.c_str(), body.length(), g_incoming)) {
            status = FetchStatus::kParseFailed;
        }
    }
    if (status == FetchStatus::kOk) {
        String body;
        if (!fetchSeries(now, "wlp", CURVE_HOURS_BEFORE, CURVE_HOURS_AFTER, CURVE_RESOLUTION,
                         body)) {
            status = FetchStatus::kHttpFailed;
        } else if (!iwlsParseCurve(body.c_str(), body.length(), g_incoming)) {
            status = FetchStatus::kParseFailed;
        }
    }
    stopWifi();

    if (status != FetchStatus::kOk) return status;

    g_incoming.fetchedAt = now;
    g_incoming.valid = true;
    data = g_incoming;
    Serial.printf("[iwls] %u extremes, %u curve points at %us spacing\n", data.extremeCount,
                  data.curve.count, data.curve.stepSec);
    return FetchStatus::kOk;
}
