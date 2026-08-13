// The tide clock screen. This is the only place that knows what the display
// looks like; the firmware and the simulator both call renderTideScreen().
#pragma once

#include "canvas.h"
#include "tide_data.h"

static const int16_t kScreenWidth = 800;
static const int16_t kScreenHeight = 480;

struct RenderStatus {
    int64_t now;         // epoch seconds
    bool hour24;         // 24-hour clock instead of am/pm
    const char* banner;  // optional warning strip, e.g. "Wi-Fi unreachable"
};

// Hours of prediction shown on the graph, relative to "now".
static const int16_t kGraphHoursBefore = 6;
static const int16_t kGraphHoursAfter = 42;

void renderTideScreen(Canvas& canvas, const TideData& data, const RenderStatus& status);

// Full-screen fallback used before the first successful download, or when the
// cached predictions have run out.
void renderMessageScreen(Canvas& canvas, const char* title, const char* line1, const char* line2);
