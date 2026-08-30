// Tide clock for the Xteink X4.
//
// The clock wakes once a day, at the boundary where the tide day on screen
// turns over. That one wake reads the battery, downloads the new day from the
// Canadian Hydrographic Service, redraws the panel and goes back to deep sleep:
// one radio session and one full-panel refresh per 24 hours, and nothing at all
// in between. The dataset lives in RTC memory so a wake that does not need the
// network costs nothing but the boot.

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

// Fingerprint of what is currently on the panel, so a wake that would draw the
// very same pixels can leave it alone. See drawnScreenId().
RTC_DATA_ATTR uint32_t g_drawnScreen;

const uint32_t kCacheMagic = 0x71DEC10CuL;
// Anything before this is a clock that has never been set.
const int64_t kPlausibleEpoch = 1700000000;

bool cacheUsable(int64_t now) {
    if (g_cacheMagic != kCacheMagic || !g_cache.valid) return false;
    int16_t mm = 0;
    if (!tideHeightAt(g_cache, now, mm)) return false;
    return tideNextExtreme(g_cache, now, true) && tideNextExtreme(g_cache, now, false);
}

// One download per tide day, taken at the boundary wake that redraws the screen
// anyway, so the radio and the panel share a single cycle.
//
// Anchoring it to the day rather than to "DATA_REFRESH_HOURS since the last
// fetch" matters. The two drift apart by however long a refresh takes, so a
// fetch that finished a minute past 6 am leaves the next download due a minute
// after the next boundary -- and the clock would wake at the boundary, find the
// download not *quite* due, redraw from the old cache, then wake a second time a
// minute later to download and redraw again. Two radio sessions and two panel
// refreshes for one day's news.
bool refreshDue(int64_t now) {
    if (g_cacheMagic != kCacheMagic || !g_cache.valid) return true;
    if (tiStartOfTideDay(now, kDayStartHour) !=
        tiStartOfTideDay(g_cache.fetchedAt, kDayStartHour)) {
        return true;
    }
    // Backstop, for a clock that somehow woke deep inside the day it fetched in.
    return now - g_cache.fetchedAt >= (int64_t)DATA_REFRESH_HOURS * 3600;
}

// Identifies the screen about to be drawn, so a wake that would put the very
// same image back on the panel can leave it alone; 0 means "never twice the
// same", which can never be skipped.
//
// The message screens are static, and a Wi-Fi outage would otherwise redraw one
// of them in full every RETRY_SLEEP_MINUTES for as long as it lasted.
//
// The tide screen is keyed on the tide day it covers, because with one wake a
// day that is the only thing that changes between draws. That is also the net
// under SLEEP_DRIFT_PERMILLE: when the RC oscillator overshoots its allowance
// and the wake lands before 6 am after all, the day comes back the same, and
// the clock puts the panel down again rather than redrawing yesterday and
// sitting on it. The status goes in too, so a refresh that starts or stops
// failing still gets its banner onto the screen.
uint32_t drawnScreenId(bool clockSet, bool haveTides, FetchStatus status, int64_t now) {
    if (!clockSet) return 0x100u;
    if (!haveTides) return 0x200u | (uint32_t)status;
    // Counted in hours, not days: consecutive tide-day boundaries are 23, 24 or
    // 25 hours apart across a daylight saving change, and dividing by 86400
    // would risk landing two of them in the same bucket.
    const uint32_t hour = (uint32_t)(tiStartOfTideDay(now, kDayStartHour) / 3600);
    return 0x80000000u | (hour << 3) | (uint32_t)status;
}

// When to come back. The screen covers one tide day, so the whole thing turns
// over at the day boundary -- new tides, new graph, and the download that
// provides them -- and that is the only moment worth waking for. The answer is
// therefore "in about 24 hours" every time, bar a failed refresh.
int64_t secondsUntilNextWake(int64_t now, bool refreshFailed) {
    // A refresh that failed has nothing new to show, so it does not wait for the
    // boundary: come back sooner and try the radio again. This is also the only
    // path that repeats a wake within a day, which is why the redraw it leads to
    // is the one the drawnScreenId() check exists to suppress.
    if (refreshFailed) {
        return (int64_t)RETRY_SLEEP_MINUTES * 60 * (1000 + SLEEP_DRIFT_PERMILLE) / 1000;
    }

    const int64_t boundary =
        tiEndOfTideDay(tiStartOfTideDay(now, kDayStartHour), kDayStartHour) +
        DAY_ROLLOVER_SECONDS;

    int64_t seconds = boundary - now;
    if (seconds < MIN_SLEEP_SECONDS) seconds = MIN_SLEEP_SECONDS;
    // Overshoot the target by the oscillator's error budget, so the wake lands
    // after the moment it was aimed at rather than either side of it. Applied
    // before the ceiling so it cannot be clamped away.
    seconds += seconds * SLEEP_DRIFT_PERMILLE / 1000;
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
    // Before anything else, because the battery latch lives in here and the
    // board runs on the cable alone until it is asserted.
    powerBegin();

    const bool manual = wokeFromButton();
    int64_t now = (int64_t)time(nullptr);
    bool clockSet = now > kPlausibleEpoch;

    FetchStatus status = FetchStatus::kOk;
    bool refreshed = false;
    if (!clockSet || manual || refreshDue(now) || !cacheUsable(now)) {
        Serial.printf("[boot] refreshing (clock %s, manual %d)\n", clockSet ? "set" : "unset",
                      (int)manual);
        refreshed = true;
        status = iwlsRefresh(g_cache);
        if (status == FetchStatus::kOk) g_cacheMagic = kCacheMagic;
        now = (int64_t)time(nullptr);
        clockSet = now > kPlausibleEpoch;
    } else {
        Serial.println("[boot] cache still good, redrawing only");
    }

    const bool haveTides = clockSet && cacheUsable(now);
    // A refresh that ran and left the clock without something to show has to be
    // retried on the short interval rather than waited out until tomorrow. The
    // download reporting kOk is not enough on its own: a response that parsed
    // but does not cover right now leaves exactly the same hole.
    const bool refreshFailed = refreshed && (status != FetchStatus::kOk || !haveTides);

    Canvas canvas(panelFramebuffer(), kScreenWidth, kScreenHeight);

    if (!clockSet) {
        renderMessageScreen(canvas, "Waiting for Wi-Fi",
                            "Could not reach the network to set the clock.",
                            "Check the credentials in secrets.h.");
    } else if (!haveTides) {
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

    // A full 800x480 refresh is by some way the most expensive thing a wake does,
    // so skip it when the panel already holds this exact image. A button press
    // always redraws: someone asked for it, and an unchanged screen is not an
    // answer.
    const uint32_t screen = drawnScreenId(clockSet, haveTides, status, now);
    if (screen != 0 && screen == g_drawnScreen && !manual) {
        Serial.println("[draw] unchanged, leaving the panel asleep");
    } else {
        panelBegin();
        panelShow(canvas);
        g_drawnScreen = screen;
    }

    // A full 800x480 refresh takes seconds, so re-read the clock rather than
    // scheduling from the timestamp the screen was drawn with.
    int64_t seconds =
        secondsUntilNextWake(clockSet ? (int64_t)time(nullptr) : now, refreshFailed);

#if STAY_AWAKE_ON_USB
    // Deep sleep powers down the USB Serial/JTAG peripheral, so the port
    // disappears from the host and esptool has nothing left to reset -- which
    // is why flashing otherwise needs the BOOT+RESET dance. On a cable there is
    // no battery to protect, so wait the schedule out awake instead and start
    // the next cycle with a reset, which keeps the port up the whole time.
    if (usbAttached()) {
        seconds = stayAwakeFor(seconds, (int64_t)USB_AWAKE_MAX_MINUTES * 60);
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
