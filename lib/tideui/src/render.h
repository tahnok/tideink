// The tide clock screen. This is the only place that knows what the display
// looks like; the firmware and the simulator both call renderTideScreen().
#pragma once

#include "canvas.h"
#include "tide_data.h"

static const int16_t kScreenWidth = 800;
static const int16_t kScreenHeight = 480;

struct RenderStatus {
    int64_t now;             // epoch seconds
    bool hour24;             // 24-hour clock instead of am/pm
    const char* banner;      // optional warning strip, e.g. "Wi-Fi unreachable"
    int16_t batteryPercent;  // 0-100, or negative to leave the battery off
    bool charging;
};

// The screen covers one tide day: this local hour through to the same hour the
// next morning, so a whole day's tides are laid out at once and the graph stops
// sliding around underneath them. Six is early enough that the day is still
// ahead of you when you first look at it.
static const int kDayStartHour = 6;

void renderTideScreen(Canvas& canvas, const TideData& data, const RenderStatus& status);

// Full-screen fallback used before the first successful download, or when the
// cached predictions have run out.
void renderMessageScreen(Canvas& canvas, const char* title, const char* line1, const char* line2);

// The last thing the clock draws before it switches itself off with a flat
// cell. E-paper holds an image without power, so this screen is what the device
// shows for however long it sits there dead -- it has to say what happened and
// what to do about it without any other context. `millivolts` is the reading
// that triggered the shutdown; `now` is the epoch it happened at, or 0 when the
// clock was never set.
void renderLowBatteryScreen(Canvas& canvas, uint16_t millivolts, int64_t now, bool hour24);
