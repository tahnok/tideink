// Tide clock for the Xteink X4.
//
// Every wake-up: read the battery, decide whether the cached predictions are
// still good, download a fresh day's worth from the Canadian Hydrographic
// Service if they are not, redraw the panel, and go back to deep sleep. The
// radio is on for a few seconds a day; everything else runs from the cache held
// in RTC memory.

#include <Arduino.h>
#include <time.h>

#include "canvas.h"
#include "config.h"
#include "iwls_client.h"
#include "panel.h"
#include "power.h"
#include "render.h"
#include "tide_data.h"
#include "time_utils.h"

namespace {

// Survives deep sleep in RTC memory (~1.2 kB), so a redraw costs no network.
RTC_DATA_ATTR uint32_t g_cacheMagic;
RTC_DATA_ATTR TideData g_cache;

const uint32_t kCacheMagic = 0x71DEC10CuL;
// Anything before this is a clock that has never been set.
const int64_t kPlausibleEpoch = 1700000000;

bool cacheUsable(int64_t now) {
    if (g_cacheMagic != kCacheMagic || !g_cache.valid) return false;
    int16_t mm = 0;
    if (!tideHeightAt(g_cache, now, mm)) return false;
    return tideNextExtreme(g_cache, now, true) && tideNextExtreme(g_cache, now, false);
}

bool refreshDue(int64_t now) {
    if (g_cacheMagic != kCacheMagic || !g_cache.valid) return true;
    return now - g_cache.fetchedAt >= (int64_t)DATA_REFRESH_HOURS * 3600;
}

int64_t secondsUntilNextWake(int64_t now) {
    int64_t target = now + (int64_t)MAX_SLEEP_MINUTES * 60;
    // MIN_SLEEP_MINUTES keeps the panel from thrashing, but it must not apply to
    // a wake aimed just after a tide: clamping a four-minute sleep up to fifteen
    // leaves an event that already happened sitting on the cards until then.
    int64_t floorSeconds = (int64_t)MIN_SLEEP_MINUTES * 60;

    // Same guard as cacheUsable()/refreshDue(): .valid alone is unvalidated RTC
    // memory until the magic says the struct is ours.
    if (g_cacheMagic == kCacheMagic && g_cache.valid) {
        const int64_t due = g_cache.fetchedAt + (int64_t)DATA_REFRESH_HOURS * 3600;
        if (due < target) target = due;
        // Wake just after the next high or low so the two headline cards always
        // point at an event that has not happened yet.
        const TideExtreme* next = tideNextAny(g_cache, now);
        if (next && next->time + WAKE_AFTER_TIDE_SECONDS < target) {
            target = next->time + WAKE_AFTER_TIDE_SECONDS;
            floorSeconds = MIN_TIDE_SLEEP_SECONDS;
        }
    }

    int64_t seconds = target - now;
    if (seconds < floorSeconds) seconds = floorSeconds;
    if (seconds > (int64_t)MAX_SLEEP_MINUTES * 60) seconds = (int64_t)MAX_SLEEP_MINUTES * 60;
    return seconds;
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.println();
    Serial.println("[boot] tide clock");

    tiSetTimezone(LOCAL_TZ);
    powerBegin();
    panelBegin();

    const bool manual = wokeFromButton();
    int64_t now = (int64_t)time(nullptr);
    bool clockSet = now > kPlausibleEpoch;

    FetchStatus status = FetchStatus::kOk;
    if (!clockSet || manual || refreshDue(now) || !cacheUsable(now)) {
        Serial.printf("[boot] refreshing (clock %s, manual %d)\n", clockSet ? "set" : "unset",
                      (int)manual);
        status = iwlsRefresh(g_cache);
        if (status == FetchStatus::kOk) g_cacheMagic = kCacheMagic;
        now = (int64_t)time(nullptr);
        clockSet = now > kPlausibleEpoch;
    } else {
        Serial.println("[boot] cache still good, redrawing only");
    }

    Canvas canvas(panelFramebuffer(), kScreenWidth, kScreenHeight);

    if (!clockSet) {
        renderMessageScreen(canvas, "Waiting for Wi-Fi",
                            "Could not reach the network to set the clock.",
                            "Check the credentials in secrets.h.");
    } else if (!cacheUsable(now)) {
        renderMessageScreen(canvas, "No tide predictions yet",
                            "Downloading from the Canadian Hydrographic Service.",
                            fetchStatusMessage(status));
    } else {
        RenderStatus st;
        st.now = now;
        st.hour24 = CLOCK_24H;
        st.banner = status == FetchStatus::kOk ? nullptr : fetchStatusMessage(status);
        renderTideScreen(canvas, g_cache, st);

        // The screen no longer shows the battery, but the log still should:
        // it is the only way to see the charge state of a sleeping clock.
        char clock[16];
        tiFormatClock(now, CLOCK_24H, clock, sizeof(clock));
        Serial.printf("[draw] %s, battery %d%%%s\n", clock, batteryPercent(),
                      batteryCharging() ? " (charging)" : "");
    }

    panelShow(canvas);

    // A full 800x480 refresh takes seconds, so re-read the clock rather than
    // scheduling from the timestamp the screen was drawn with.
    deepSleepFor(secondsUntilNextWake(clockSet ? (int64_t)time(nullptr) : now));
}

void loop() {
    // Never reached: setup() ends in deep sleep, which restarts the sketch.
}
