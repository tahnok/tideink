#include "render.h"

#include <stdio.h>
#include <string.h>

#include "fonts/fonts.h"
#include "time_utils.h"

namespace {

const int16_t kMargin = 16;
const int16_t kHeaderH = 56;  // only the message screen still draws a header
const int16_t kCardY = 16;
const int16_t kCardH = 96;
// Wide enough that the right-aligned height on the left card clears the rule
// between the two, while both cards still line up with the page margins.
const int16_t kCardGap = 24;

const int16_t kPlotY = 160;
const int16_t kPlotH = 258;

// Warning strip above the cards. Everything below it shifts down by this much
// and the plot gives up the height, so the cards keep their full size.
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

// One of the two "next tide" panels. There is no box around them: a rule
// between the pair and one underneath separate them well enough, and the height
// that a border and a title bar would have cost goes to the graph instead. The
// high/low word is set at body size rather than in small caps because it is the
// one thing on the screen you read from across the room.
void drawTideCard(Canvas& c, int16_t x, int16_t y, int16_t w, int16_t h, bool high,
                  const TideExtreme* ev, const RenderStatus& st) {
    const int16_t arrowX = x + 10;
    const int16_t titleBase = y + 26;
    if (high) {
        c.fillTriangleUp(arrowX, titleBase, 10, 20, kBlack);
    } else {
        c.fillTriangleDown(arrowX, titleBase - 20, 10, 20, kBlack);
    }
    c.drawTextTracked(arrowX + 24, titleBase, high ? "HIGH TIDE" : "LOW TIDE", FontH2, 2, kBlack);

    if (!ev) {
        c.drawTextAligned(x + w / 2, y + 76, "no prediction", FontH1, kCenter);
        return;
    }

    char height[16];
    formatMetres(ev->heightMm, height, sizeof(height));
    c.drawTextAligned(x + w, titleBase, height, FontH2, kRight);

    char clock[16], suffix[8];
    tiFormatClockParts(ev->time, st.hour24, clock, sizeof(clock), suffix, sizeof(suffix));

    const int16_t base = y + h - 4;
    int16_t cursor = x + 6;
    cursor += c.drawText(cursor, base, clock, FontBig);
    if (suffix[0]) {
        c.drawText(cursor + 8, base - 4, suffix, FontH2);
    }
}

struct PlotMap {
    int64_t from;
    int64_t to;
    int16_t minMm;
    int16_t maxMm;
    int16_t top;     // a banner pushes the plot down and shortens it
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

void drawGraph(Canvas& c, const TideData& data, const RenderStatus& st, int16_t plotTop,
               int16_t plotH) {
    PlotMap map;
    map.from = st.now - (int64_t)kGraphHoursBefore * 3600;
    map.to = st.now + (int64_t)kGraphHoursAfter * 3600;
    map.top = plotTop;
    map.height = plotH;

    int16_t lo = 0, hi = 0;
    if (!tideCurveRange(data, map.from, map.to, lo, hi)) {
        c.drawTextAligned(kScreenWidth / 2, plotTop + plotH / 2, "no water level predictions",
                          FontBody, kCenter);
        return;
    }
    // Round the axis out to a tidy step so the gridline labels read as 0.2,
    // 0.4, ... rather than whatever the padded data range happens to be.
    const int16_t stepMm = niceStepMm(hi - lo);
    map.minMm = (int16_t)(floorDiv(lo - stepMm / 3, stepMm) * stepMm);
    map.maxMm = (int16_t)(ceilDiv(hi + stepMm / 3, stepMm) * stepMm);

    // Section heading.
    c.drawTextTracked(kMargin + 6, plotTop - 14, "WATER LEVEL, NEXT 48 HOURS", FontLabel, 2);
    c.drawTextAligned(kPlotRight, plotTop - 14, "metres above chart datum", FontTiny, kRight);

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

    // Day boundaries and 6-hourly ticks along the bottom.
    const int16_t axisY = plotTop + plotH;
    c.hLine(kPlotLeft, axisY, kPlotRight - kPlotLeft, kBlack);
    c.vLine(kPlotLeft, plotTop, plotH + 1, kBlack);

    // Walk local hours rather than UTC ones: the ticks have to land on local
    // 00/06/12/18 whatever the current UTC offset is, and stay right across a
    // daylight saving change.
    for (int64_t t = (map.from / 3600) * 3600; t <= map.to; t += 3600) {
        if (t < map.from) continue;
        const int hour = tiLocalHour(t);
        if (hour % 6 != 0) continue;
        const int16_t x = map.xFor(t);
        const bool midnight = hour == 0;
        c.vLine(x, axisY + 1, midnight ? 8 : 4, kBlack);
        if (midnight) c.dashedVLine(x, plotTop, plotH, 2, 6, kBlack);
        char label[16];
        if (midnight) {
            char day[8];
            tiFormatShortDay(t, day, sizeof(day));
            snprintf(label, sizeof(label), "%s", day);
            c.drawTextAligned(x, axisY + 23, label, FontLabel, kCenter);
        } else {
            if (st.hour24) {
                snprintf(label, sizeof(label), "%02d", hour);
            } else {
                const int h12 = hour % 12 == 0 ? 12 : hour % 12;
                snprintf(label, sizeof(label), "%d%s", h12, hour < 12 ? "a" : "p");
            }
            c.drawTextAligned(x, axisY + 22, label, FontTiny, kCenter);
        }
    }

    // High/low markers with labels.
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

        char label[40], clock[16];
        tiFormatClock(e.time, st.hour24, clock, sizeof(clock));
        char metres[12];
        formatMetresBare(e.heightMm, metres, sizeof(metres));
        snprintf(label, sizeof(label), "%s  %sm", clock, metres);
        const int16_t w = Canvas::textWidth(label, FontTiny);
        int16_t lx = x;
        if (lx - w / 2 < kPlotLeft + 2) lx = kPlotLeft + 2 + w / 2;
        if (lx + w / 2 > kPlotRight - 2) lx = kPlotRight - 2 - w / 2;
        const int16_t ly = e.high ? y - 12 : y + 20;
        c.fillRect(lx - w / 2 - 3, ly - 12, w + 6, 16, kWhite);
        c.drawTextAligned(lx, ly, label, FontTiny, kCenter);
    }
}

void drawFooter(Canvas& c, const TideData& data, const RenderStatus& st) {
    const int16_t base = kScreenHeight - 9;
    c.hLine(kMargin, kFooterRuleY, kScreenWidth - 2 * kMargin, kBlack);

    c.drawText(kMargin + 6, base,
               "Predictions: Fisheries and Oceans Canada, Canadian Hydrographic Service", FontTiny);

    char right[96];
    if (data.fetchedAt > 0) {
        char age[24];
        tiFormatAge(st.now - data.fetchedAt, age, sizeof(age));
        char when[32];
        tiFormatDateTime(data.fetchedAt, st.hour24, when, sizeof(when));
        snprintf(right, sizeof(right), "updated %s (%s)", when, age);
    } else {
        snprintf(right, sizeof(right), "never updated");
    }
    c.drawTextAligned(kScreenWidth - kMargin - 6, base, right, FontTiny, kRight);
}

void drawBanner(Canvas& c, const char* text) {
    const int16_t y = kCardY;
    c.fillRectPattern(kMargin, y, kScreenWidth - 2 * kMargin, kBannerH - 4, kCheck50, kBlack);
    const int16_t w = Canvas::textWidth(text, FontLabel, 2);
    c.fillRect(kScreenWidth / 2 - w / 2 - 12, y, w + 24, kBannerH - 4, kWhite);
    c.drawTextTracked(kScreenWidth / 2 - w / 2, y + 15, text, FontLabel, 2, kBlack);
}

}  // namespace

void renderTideScreen(Canvas& c, const TideData& data, const RenderStatus& st) {
    c.clear(kWhite);

    if (st.banner) drawBanner(c, st.banner);

    // A banner pushes the cards down at their full height; the plot, which has
    // the most room to spare, gives up the difference.
    const int16_t shift = st.banner ? kBannerH : 0;
    const int16_t cardW = (kScreenWidth - 2 * kMargin - kCardGap) / 2;
    const int16_t cardY = kCardY + shift;
    drawTideCard(c, kMargin, cardY, cardW, kCardH, true, tideNextExtreme(data, st.now, true), st);
    drawTideCard(c, kMargin + cardW + kCardGap, cardY, cardW, kCardH, false,
                 tideNextExtreme(data, st.now, false), st);
    c.vLine(kScreenWidth / 2, cardY + 4, kCardH - 8, kBlack);
    c.hLine(kMargin, cardY + kCardH + 14, kScreenWidth - 2 * kMargin, kBlack);

    drawGraph(c, data, st, kPlotY + shift, kPlotH - shift);
    drawFooter(c, data, st);
}

void renderMessageScreen(Canvas& c, const char* title, const char* line1, const char* line2) {
    c.clear(kWhite);
    c.fillRect(0, 0, kScreenWidth, kHeaderH, kBlack);
    c.drawTextTracked(kMargin + 6, 38, "TIDE CLOCK", FontH1, 2, kWhite);

    c.drawTextAligned(kScreenWidth / 2, 210, title, FontH1, kCenter);
    if (line1) c.drawTextAligned(kScreenWidth / 2, 260, line1, FontBody, kCenter);
    if (line2) c.drawTextAligned(kScreenWidth / 2, 292, line2, FontBody, kCenter);
    c.drawRectThick(kMargin, kHeaderH + 24, kScreenWidth - 2 * kMargin,
                    kScreenHeight - kHeaderH - 48, 2, kBlack);
}
