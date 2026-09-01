#include "render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fonts/fonts.h"
#include "moon.h"
#include "time_utils.h"

namespace {

const int16_t kMargin = 16;
const int16_t kHeaderH = 56;  // only the message screen still draws a header

// Top band: the day's tides on the left, the moon on the right.
const int16_t kBandY = 16;
const int16_t kBandH = 96;
const int16_t kBandRuleY = kBandY + kBandH + 12;
const int16_t kMoonW = 152;  // "Waning crescent" is the widest phase name
const int16_t kMoonLeft = kScreenWidth - kMargin - kMoonW;
const int16_t kDividerX = kMoonLeft - 14;
const int16_t kTidesRight = kDividerX - 12;
const uint8_t kMaxShownExtremes = 6;

const int16_t kPlotY = 160;
const int16_t kPlotH = 258;

// Warning strip above the band. Everything below it shifts down by this much
// and the plot gives up the height, so the band keeps its full size.
const int16_t kBannerH = 26;
const int16_t kPlotLeft = 62;  // room for the height axis labels
const int16_t kPlotRight = kScreenWidth - kMargin;
const int16_t kFooterRuleY = 452;

void formatMetres(int16_t mm, char* out, size_t n) {
    const int32_t hundredths = ((int32_t)mm + (mm >= 0 ? 5 : -5)) / 10;
    snprintf(out, n, "%s%d.%02d m", hundredths < 0 ? "-" : "",
             (int)(hundredths < 0 ? -hundredths : hundredths) / 100,
             (int)(hundredths < 0 ? -hundredths : hundredths) % 100);
}

void formatMetresBare(int16_t mm, char* out, size_t n) {
    const int32_t tenths = ((int32_t)mm + (mm >= 0 ? 50 : -50)) / 100;
    snprintf(out, n, "%s%d.%d", tenths < 0 ? "-" : "", (int)(tenths < 0 ? -tenths : tenths) / 10,
             (int)(tenths < 0 ? -tenths : tenths) % 10);
}

void upperCase(char* s) {
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - ('a' - 'A'));
    }
}

int32_t floorDiv(int32_t a, int32_t b) { return a >= 0 ? a / b : -(((-a) + b - 1) / b); }
int32_t ceilDiv(int32_t a, int32_t b) { return a >= 0 ? (a + b - 1) / b : -((-a) / b); }

// Gridline spacing that keeps 3-6 lines on the axis with a readable label.
int16_t niceStepMm(int32_t rangeMm) {
    static const int16_t kSteps[] = {50, 100, 200, 250, 500, 1000, 2000, 5000};
    for (size_t i = 0; i < sizeof(kSteps) / sizeof(kSteps[0]); i++) {
        if (rangeMm / kSteps[i] <= 5) return kSteps[i];
    }
    return kSteps[sizeof(kSteps) / sizeof(kSteps[0]) - 1];
}

// One tide in the day's row. There is no box around these: a rule under the
// whole band separates them from the graph well enough, and the height a border
// and a title bar would have cost goes to the graph instead. The high/low word
// is what you read from across the room, so it is set in tracked caps and the
// time under it as large as the column allows.
void drawTideEntry(Canvas& c, int16_t x, int16_t y, int16_t w, const TideExtreme& ev,
                   const RenderStatus& st, bool next) {
    const int16_t arrowX = x + 9;
    const int16_t labelBase = y + 18;
    if (ev.high) {
        c.fillTriangleUp(arrowX, labelBase, 7, 14, kBlack);
    } else {
        c.fillTriangleDown(arrowX, labelBase - 14, 7, 14, kBlack);
    }
    c.drawTextTracked(arrowX + 15, labelBase, ev.high ? "HIGH" : "LOW", FontLabel, 2, kBlack);

    char clock[16], suffix[8];
    tiFormatClockParts(ev.time, st.hour24, clock, sizeof(clock), suffix, sizeof(suffix));

    // Four tides leave room for the headline size at any time of day. A station
    // that somehow reports more gets a narrower column than "12:59 PM" fits, so
    // drop a size rather than let the columns collide.
    const int16_t suffixW = suffix[0] ? Canvas::textWidth(suffix, FontLabel) + 6 : 0;
    const TiFont& timeFont =
        Canvas::textWidth(clock, FontH1) + suffixW <= w - 12 ? FontH1 : FontH2;
    const int16_t timeBase = y + 54;
    int16_t cursor = x + 6;
    cursor += c.drawText(cursor, timeBase, clock, timeFont);
    if (suffix[0]) c.drawText(cursor + 6, timeBase, suffix, FontLabel);

    char height[16];
    formatMetres(ev.heightMm, height, sizeof(height));
    c.drawText(x + 6, y + 78, height, FontH2);

    // The row is chronological and the graph below has a "now" cursor, but a bar
    // under the column that is still ahead of you saves reading either. It runs
    // the width of the column rather than the width of the text, so it reads as
    // a marker for the whole tide and not as an underline on the height.
    if (next) c.fillRect(x, y + kBandH - 4, w - 8, 3, kBlack);
}

void drawTideRow(Canvas& c, const TideData& data, const RenderStatus& st, int64_t from, int64_t to,
                 int16_t y) {
    const TideExtreme* shown[kMaxShownExtremes];
    const uint8_t n = tideExtremesIn(data, from, to, shown, kMaxShownExtremes);
    if (n == 0) {
        c.drawTextAligned((kMargin + kTidesRight) / 2, y + 58, "no predictions for today", FontH2,
                          kCenter);
        return;
    }

    const int16_t w = (kTidesRight - kMargin) / n;
    for (uint8_t i = 0; i < n; i++) {
        const bool next = shown[i]->time >= st.now && (i == 0 || shown[i - 1]->time < st.now);
        drawTideEntry(c, kMargin + i * w, y, w, *shown[i], st, next);
    }
}

// The lit limb is paper, the shadowed part is ink -- so a new moon is a solid
// disc and a full moon an empty ring, the way a monochrome moon has always been
// drawn. The terminator is the ellipse you get by projecting the great circle
// dividing day from night, which is one cosine per row.
void drawMoon(Canvas& c, int16_t cx, int16_t cy, int16_t r, const MoonPhase& m) {
    const double cosPhase = cos(2.0 * M_PI * (double)m.fraction);
    for (int16_t dy = -r; dy <= r; dy++) {
        const int16_t w = (int16_t)(sqrt((double)(r * r - dy * dy)) + 0.5);
        // Waxing lights the right limb, waning the left; the terminator sweeps
        // from one edge to the other and back over the month.
        const int16_t litFrom = m.waxing ? (int16_t)(cosPhase * w) : (int16_t)-w;
        const int16_t litTo = m.waxing ? w : (int16_t)(-cosPhase * w);
        for (int16_t dx = -w; dx <= w; dx++) {
            if (dx >= litFrom && dx <= litTo) continue;
            c.setPixel(cx + dx, cy + dy, kBlack);
        }
    }
    c.drawCircle(cx, cy, r, kBlack);
}

void drawMoonPanel(Canvas& c, int64_t now, int16_t y) {
    const MoonPhase m = moonPhaseAt(now);
    const int16_t cx = kMoonLeft + kMoonW / 2;

    c.vLine(kDividerX, y + 4, kBandH - 8, kBlack);
    drawMoon(c, cx, y + 28, 24, m);
    c.drawTextAligned(cx, y + 74, m.name, FontLabel, kCenter);

    char lit[24];
    snprintf(lit, sizeof(lit), "%d%% lit", (int)(m.illumination * 100.0f + 0.5f));
    c.drawTextAligned(cx, y + 92, lit, FontTiny, kCenter);
}

struct PlotMap {
    int64_t from;
    int64_t to;
    int16_t minMm;
    int16_t maxMm;
    int16_t top;  // a banner pushes the plot down and shortens it
    int16_t height;

    int16_t xFor(int64_t t) const {
        if (to <= from) return kPlotLeft;
        const int64_t span = to - from;
        int64_t px = (int64_t)(kPlotRight - kPlotLeft) * (t - from) / span;
        if (px < 0) px = 0;
        if (px > kPlotRight - kPlotLeft) px = kPlotRight - kPlotLeft;
        return kPlotLeft + (int16_t)px;
    }

    int64_t timeFor(int16_t x) const {
        const int64_t span = to - from;
        return from + span * (x - kPlotLeft) / (kPlotRight - kPlotLeft);
    }

    int16_t yFor(int16_t mm) const {
        const int32_t range = maxMm - minMm;
        if (range <= 0) return top + height / 2;
        int32_t py = (int32_t)(height - 1) * (mm - minMm) / range;
        if (py < 0) py = 0;
        if (py > height - 1) py = height - 1;
        return top + height - 1 - (int16_t)py;
    }
};

// Centred, but never hanging off either end of the axis.
void drawAxisLabel(Canvas& c, int16_t x, int16_t baseline, const char* text, const TiFont& font) {
    const int16_t half = Canvas::textWidth(text, font) / 2;
    if (x - half < kPlotLeft) x = kPlotLeft + half;
    if (x + half > kPlotRight) x = kPlotRight - half;
    c.drawTextAligned(x, baseline, text, font, kCenter);
}

void drawGraph(Canvas& c, const TideData& data, const RenderStatus& st, int64_t from, int64_t to,
               int16_t plotTop, int16_t plotH) {
    PlotMap map;
    map.from = from;
    map.to = to;
    map.top = plotTop;
    map.height = plotH;

    {
        int16_t dlo = 0, dhi = 0;
        if (!tideCurveRange(data, map.from, map.to, dlo, dhi)) {
            c.drawTextAligned(kScreenWidth / 2, plotTop + plotH / 2, "no water level predictions",
                              FontBody, kCenter);
            return;
        }
    }
    const int16_t stepMm = niceStepMm(kGraphMaxMm - kGraphMinMm);
    map.minMm = (int16_t)(floorDiv(kGraphMinMm, stepMm) * stepMm);
    map.maxMm = (int16_t)(ceilDiv(kGraphMaxMm, stepMm) * stepMm);

    int16_t nowMm = 0;
    const bool haveNow =
        st.now >= map.from && st.now <= map.to && tideHeightAt(data, st.now, nowMm);

    // Section heading: the window is fixed to the day, so name the day.
    char day[16], hourLabel[12], heading[72];
    tiFormatDayDate(map.from, day, sizeof(day));
    tiFormatHourOfDay(kDayStartHour, st.hour24, hourLabel, sizeof(hourLabel));
    snprintf(heading, sizeof(heading), "WATER LEVEL - %s, %s TO %s", day, hourLabel, hourLabel);
    upperCase(heading);
    c.drawTextTracked(kMargin + 6, plotTop - 14, heading, FontLabel, 2);

    char units[48];
    if (haveNow) {
        char level[16];
        formatMetres(nowMm, level, sizeof(level));
        snprintf(units, sizeof(units), "now %s above chart datum", level);
    } else {
        snprintf(units, sizeof(units), "metres above chart datum");
    }
    c.drawTextAligned(kPlotRight, plotTop - 14, units, FontTiny, kRight);

    // Horizontal gridlines and height labels.
    for (int32_t mm = map.minMm; mm <= map.maxMm; mm += stepMm) {
        const int16_t y = map.yFor((int16_t)mm);
        c.dashedHLine(kPlotLeft, y, kPlotRight - kPlotLeft, 1, 5);
        char label[12];
        formatMetresBare((int16_t)mm, label, sizeof(label));
        c.drawTextAligned(kPlotLeft - 10, y + 5, label, FontTiny, kRight);
    }

    // Curve, with a dithered fill below it.
    int16_t prevY = -1;
    for (int16_t x = kPlotLeft; x <= kPlotRight; x++) {
        int16_t mm = 0;
        if (!tideHeightAt(data, map.timeFor(x), mm)) {
            prevY = -1;
            continue;
        }
        const int16_t y = map.yFor(mm);
        for (int16_t fy = y + 1; fy < plotTop + plotH; fy++) {
            if ((x % 2) == 0 && (fy % 2) == 0) c.setPixel(x, fy, kBlack);
        }
        if (prevY >= 0 && (y > prevY + 1 || y < prevY - 1)) {
            c.vLine(x, y < prevY ? y : prevY, (y > prevY ? y - prevY : prevY - y) + 1);
            c.vLine(x, (y < prevY ? y : prevY) + 1, (y > prevY ? y - prevY : prevY - y));
        }
        c.setPixel(x, y, kBlack);
        c.setPixel(x, y + 1, kBlack);
        prevY = y;
    }

    const int16_t axisY = plotTop + plotH;
    c.hLine(kPlotLeft, axisY, kPlotRight - kPlotLeft, kBlack);
    c.vLine(kPlotLeft, plotTop, plotH + 1, kBlack);

    // Walk local hours rather than UTC ones: the ticks have to land on local
    // clock hours whatever the current UTC offset is, and stay right across a
    // daylight saving change. A day fits in the width comfortably enough for an
    // hourly tick, so only every third one is labelled.
    for (int64_t t = map.from; t <= map.to; t += 3600) {
        const int hour = tiLocalHour(t);
        const int16_t x = map.xFor(t);
        const bool midnight = hour == 0;
        const bool labelled = hour % 3 == 0;
        c.vLine(x, axisY + 1, midnight ? 9 : (labelled ? 6 : 3), kBlack);
        if (!labelled) continue;
        if (midnight) {
            // Midnight is the one place the date changes, so it gets the day
            // name instead of an hour and a rule up through the plot.
            c.dashedVLine(x, plotTop, plotH, 2, 6, kBlack);
            char label[8];
            tiFormatShortDay(t, label, sizeof(label));
            drawAxisLabel(c, x, axisY + 23, label, FontLabel);
        } else {
            char label[16];
            if (st.hour24) {
                snprintf(label, sizeof(label), "%02d", hour);
            } else {
                const int h12 = hour % 12 == 0 ? 12 : hour % 12;
                snprintf(label, sizeof(label), "%d%s", h12, hour < 12 ? "a" : "p");
            }
            drawAxisLabel(c, x, axisY + 22, label, FontTiny);
        }
    }

    // High/low markers. The row above the graph carries the times and heights,
    // so these are left unlabelled.
    const int64_t curveEnd =
        data.curve.startTime + (int64_t)(data.curve.count - 1) * data.curve.stepSec;
    for (uint8_t i = 0; i < data.extremeCount; i++) {
        const TideExtreme& e = data.extremes[i];
        if (e.time < map.from || e.time > map.to) continue;
        // Only mark extremes that sit on a drawn stretch of curve.
        if (e.time < data.curve.startTime || e.time > curveEnd) continue;
        const int16_t x = map.xFor(e.time);
        const int16_t y = map.yFor(e.heightMm);
        c.fillCircle(x, y, 4, kWhite);
        c.drawCircle(x, y, 4, kBlack);
        c.fillCircle(x, y, 2, kBlack);
    }

    // "Now" cursor. The window no longer moves with the clock, so this is what
    // tells you how far into the day you are. Denser dashes than the midnight
    // rule, with a solid pointer at each end.
    if (haveNow) {
        const int16_t x = map.xFor(st.now);
        c.dashedVLine(x, plotTop, plotH, 5, 2, kBlack);
        // Inside the plot rather than above it: the heading row is already busy,
        // and the padding niceStepMm() leaves on the axis keeps the curve clear
        // of the top.
        c.fillTriangleDown(x, plotTop + 1, 5, 9, kBlack);
        const int16_t y = map.yFor(nowMm);
        c.fillCircle(x, y, 4, kWhite);
        c.fillCircle(x, y, 3, kBlack);
    }
}

// A battery outline filled in proportion, then the percentage. Small, because
// on a clock that runs for months it is only ever a reassurance.
void drawBattery(Canvas& c, int16_t right, int16_t baseline, int16_t percent, bool charging) {
    if (percent < 0) return;
    if (percent > 100) percent = 100;

    char text[24];
    snprintf(text, sizeof(text), charging ? "%d%% charging" : "%d%%", (int)percent);
    c.drawTextAligned(right, baseline, text, FontTiny, kRight);

    const int16_t bodyW = 24, bodyH = 12;
    const int16_t x = right - Canvas::textWidth(text, FontTiny) - 8 - bodyW - 3;
    const int16_t y = baseline - 10;
    c.drawRect(x, y, bodyW, bodyH, kBlack);
    c.fillRect(x + bodyW, y + 4, 3, bodyH - 8, kBlack);  // terminal nub
    const int16_t fill = (int16_t)((int32_t)(bodyW - 4) * percent / 100);
    if (fill > 0) c.fillRect(x + 2, y + 2, fill, bodyH - 4, kBlack);
}

void drawFooter(Canvas& c, const RenderStatus& st) {
    const int16_t base = kScreenHeight - 9;
    c.hLine(kMargin, kFooterRuleY, kScreenWidth - 2 * kMargin, kBlack);

    c.drawText(kMargin + 6, base,
               "Predictions: Fisheries and Oceans Canada, Canadian Hydrographic Service", FontTiny);

    drawBattery(c, kScreenWidth - kMargin - 6, base, st.batteryPercent, st.charging);
}

// Header band and border shared by every full-screen state, so a message and a
// shutdown notice are recognisably the same object as the tide screen.
void drawFullScreenChrome(Canvas& c) {
    c.fillRect(0, 0, kScreenWidth, kHeaderH, kBlack);
    c.drawTextTracked(kMargin + 6, 38, "TIDE CLOCK", FontH1, 2, kWhite);
    c.drawRectThick(kMargin, kHeaderH + 24, kScreenWidth - 2 * kMargin,
                    kScreenHeight - kHeaderH - 48, 2, kBlack);
}

// A large, deliberately empty battery. The footer gauge is a reassurance drawn
// small; this one is the message, so it is drawn at a size that reads as a
// symbol rather than an indicator, and nothing is ever filled in behind it.
void drawEmptyBatteryGlyph(Canvas& c, int16_t centreX, int16_t top, int16_t bodyW, int16_t bodyH) {
    const int16_t nubW = 9, nubH = bodyH / 3;
    const int16_t x = centreX - (bodyW + nubW) / 2;
    c.drawRectThick(x, top, bodyW, bodyH, 5, kBlack);
    c.fillRect(x + bodyW, top + (bodyH - nubH) / 2, nubW, nubH, kBlack);
}

void drawBanner(Canvas& c, const char* text) {
    const int16_t y = kBandY;
    c.fillRectPattern(kMargin, y, kScreenWidth - 2 * kMargin, kBannerH - 4, kCheck50, kBlack);
    const int16_t w = Canvas::textWidth(text, FontLabel, 2);
    c.fillRect(kScreenWidth / 2 - w / 2 - 12, y, w + 24, kBannerH - 4, kWhite);
    c.drawTextTracked(kScreenWidth / 2 - w / 2, y + 15, text, FontLabel, 2, kBlack);
}

}  // namespace

void renderTideScreen(Canvas& c, const TideData& data, const RenderStatus& st) {
    c.clear(kWhite);

    if (st.banner) drawBanner(c, st.banner);

    // A banner pushes the band down at its full height; the plot, which has the
    // most room to spare, gives up the difference.
    const int16_t shift = st.banner ? kBannerH : 0;
    const int64_t from = tiStartOfTideDay(st.now, kDayStartHour);
    const int64_t to = tiEndOfTideDay(from, kDayStartHour);

    drawTideRow(c, data, st, from, to, kBandY + shift);
    drawMoonPanel(c, st.now, kBandY + shift);
    c.hLine(kMargin, kBandRuleY + shift, kScreenWidth - 2 * kMargin, kBlack);

    drawGraph(c, data, st, from, to, kPlotY + shift, kPlotH - shift);
    drawFooter(c, st);
}

void renderMessageScreen(Canvas& c, const char* title, const char* line1, const char* line2) {
    c.clear(kWhite);
    drawFullScreenChrome(c);

    c.drawTextAligned(kScreenWidth / 2, 210, title, FontH1, kCenter);
    if (line1) c.drawTextAligned(kScreenWidth / 2, 260, line1, FontBody, kCenter);
    if (line2) c.drawTextAligned(kScreenWidth / 2, 292, line2, FontBody, kCenter);
}

void renderLowBatteryScreen(Canvas& c, uint16_t millivolts, int64_t now, bool hour24) {
    c.clear(kWhite);
    drawFullScreenChrome(c);

    drawEmptyBatteryGlyph(c, kScreenWidth / 2, 106, 220, 96);

    // The headline is the one thing that has to carry across a room, so it gets
    // the largest face in the build and the tracking the other all-caps labels
    // on the clock use.
    const int16_t w = Canvas::textWidth("CHARGE ME", FontBig, 4);
    c.drawTextTracked(kScreenWidth / 2 - w / 2, 292, "CHARGE ME", FontBig, 4, kBlack);

    c.drawTextAligned(kScreenWidth / 2, 348,
                      "The battery is flat and the clock has switched itself off.", FontBody,
                      kCenter);
    c.drawTextAligned(kScreenWidth / 2, 380,
                      "Plug in USB and it picks the tides back up on its own.", FontBody, kCenter);

    // Small print: enough to tell how long it has been sitting here, and what
    // the cell actually read, without competing with the headline.
    char note[80];
    const int32_t hundredths = ((int32_t)millivolts + 5) / 10;
    if (now > 0) {
        char clock[16], day[16];
        tiFormatClock(now, hour24, clock, sizeof(clock));
        tiFormatDayDate(now, day, sizeof(day));
        snprintf(note, sizeof(note), "Cell %d.%02d V - switched off %s at %s",
                 (int)hundredths / 100, (int)hundredths % 100, day, clock);
    } else {
        snprintf(note, sizeof(note), "Cell %d.%02d V", (int)hundredths / 100,
                 (int)hundredths % 100);
    }
    c.drawTextAligned(kScreenWidth / 2, 424, note, FontTiny, kCenter);
}
