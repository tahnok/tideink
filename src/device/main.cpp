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
    // MIN_SLEEP_MINUTES keeps the panel from thrashing, but it must not apply to
    // a wake aimed at a moment on the clock: clamping a four-minute sleep up to
    // fifteen leaves an event that already happened sitting on screen until then.
    // Each candidate therefore carries the floor that goes with it, and only the
    // winner's floor is applied.
    int64_t target = now + (int64_t)MAX_SLEEP_MINUTES * 60;
    int64_t floorSeconds = (int64_t)MIN_SLEEP_MINUTES * 60;
    // A target in the past is kept rather than skipped -- a download that is
    // already overdue because the last one failed wants the shortest sleep the
    // clamp below allows, not the next thing on the calendar.
    auto consider = [&](int64_t when, int64_t floorSec) {
        if (when < target) {
            target = when;
            floorSeconds = floorSec;
        }
    };

    // The screen covers one tide day, so the whole thing turns over at the day
    // boundary. Without a wake aimed at it the clock could sit on yesterday's
    // tides for most of a morning.
    consider(tiEndOfTideDay(tiStartOfTideDay(now, kDayStartHour), kDayStartHour) +
                 DAY_ROLLOVER_SECONDS,
             MIN_TIDE_SLEEP_SECONDS);

    // Same guard as cacheUsable()/refreshDue(): .valid alone is unvalidated RTC
    // memory until the magic says the struct is ours.
    if (g_cacheMagic == kCacheMagic && g_cache.valid) {
        consider(g_cache.fetchedAt + (int64_t)DATA_REFRESH_HOURS * 3600,
                 (int64_t)MIN_SLEEP_MINUTES * 60);
        // Wake just after the next high or low so the row of tides always has an
        // event that has not happened yet marked as the next one.
        const TideExtreme* next = tideNextAny(g_cache, now);
        if (next) consider(next->time + WAKE_AFTER_TIDE_SECONDS, MIN_TIDE_SLEEP_SECONDS);
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
        st.batteryPercent = batteryPercent();
        st.charging = batteryCharging();
        renderTideScreen(canvas, g_cache, st);

        char clock[16];
        tiFormatClock(now, CLOCK_24H, clock, sizeof(clock));
        Serial.printf("[draw] %s, battery %d%%%s\n", clock, st.batteryPercent,
                      st.charging ? " (charging)" : "");
    }

    panelShow(canvas);

    // A full 800x480 refresh takes seconds, so re-read the clock rather than
    // scheduling from the timestamp the screen was drawn with.
    int64_t seconds = secondsUntilNextWake(clockSet ? (int64_t)time(nullptr) : now);

#if STAY_AWAKE_ON_USB
    // Deep sleep powers down the USB Serial/JTAG peripheral, so the port
    // disappears from the host and esptool has nothing left to reset -- which
    // is why flashing otherwise needs the BOOT+RESET dance. On a cable there is
    // no battery to protect, so wait the schedule out awake instead and start
    // the next cycle with a reset, which keeps the port up the whole time.
    if (usbAttached()) {
        seconds = stayAwakeFor(seconds);
        if (seconds == 0) {
            Serial.println("[power] restarting for the next cycle");
            Serial.flush();
            ESP.restart();
        }
    }
#endif

    deepSleepFor(seconds);
}

void loop() {
    // Never reached: setup() ends in deep sleep, which restarts the sketch.
}
