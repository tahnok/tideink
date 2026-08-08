#include "canvas.h"

#include <string.h>

namespace {

// Each pattern is 8 rows of an 8x8 tile; a set bit means "draw ink here".
const uint8_t kPatternTiles[][8] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},  // kSolid
    {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55},  // kCheck50
    {0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22},  // kDots25
    {0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00},  // kDots12
    {0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44, 0x88},  // kHatchRight
};

}  // namespace

Canvas::Canvas(uint8_t* buffer, int16_t width, int16_t height)
    : buffer_(buffer), width_(width), height_(height), stride_((width + 7) / 8) {}

void Canvas::clear(TiInk ink) {
    memset(buffer_, ink == kBlack ? 0xFF : 0x00, sizeBytes());
}

void Canvas::setPixel(int16_t x, int16_t y, TiInk ink) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    uint8_t* byte = &buffer_[(uint32_t)y * stride_ + (x >> 3)];
    const uint8_t mask = 0x80 >> (x & 7);
    if (ink == kBlack) {
        *byte |= mask;
    } else {
        *byte &= ~mask;
    }
}

TiInk Canvas::pixel(int16_t x, int16_t y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return kWhite;
    const uint8_t byte = buffer_[(uint32_t)y * stride_ + (x >> 3)];
    return (byte & (0x80 >> (x & 7))) ? kBlack : kWhite;
}

void Canvas::hLine(int16_t x, int16_t y, int16_t w, TiInk ink) {
    for (int16_t i = 0; i < w; i++) setPixel(x + i, y, ink);
}

void Canvas::vLine(int16_t x, int16_t y, int16_t h, TiInk ink) {
    for (int16_t i = 0; i < h; i++) setPixel(x, y + i, ink);
}

void Canvas::line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, TiInk ink) {
    // Bresenham.
    int16_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int16_t dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const int16_t sx = x0 < x1 ? 1 : -1;
    const int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx - dy;
    while (true) {
        setPixel(x0, y0, ink);
        if (x0 == x1 && y0 == y1) break;
        const int16_t e2 = err * 2;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Canvas::dashedVLine(int16_t x, int16_t y, int16_t h, uint8_t on, uint8_t off, TiInk ink) {
    const uint16_t period = on + off;
    if (period == 0) return;
    for (int16_t i = 0; i < h; i++) {
        if (i % period < on) setPixel(x, y + i, ink);
    }
}

void Canvas::dashedHLine(int16_t x, int16_t y, int16_t w, uint8_t on, uint8_t off, TiInk ink) {
    const uint16_t period = on + off;
    if (period == 0) return;
    for (int16_t i = 0; i < w; i++) {
        if (i % period < on) setPixel(x + i, y, ink);
    }
}

void Canvas::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, TiInk ink) {
    for (int16_t j = 0; j < h; j++) hLine(x, y + j, w, ink);
}

void Canvas::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, TiInk ink) {
    if (w <= 0 || h <= 0) return;
    hLine(x, y, w, ink);
    hLine(x, y + h - 1, w, ink);
    vLine(x, y, h, ink);
    vLine(x + w - 1, y, h, ink);
}

void Canvas::drawRectThick(int16_t x, int16_t y, int16_t w, int16_t h, int16_t t, TiInk ink) {
    for (int16_t i = 0; i < t; i++) drawRect(x + i, y + i, w - 2 * i, h - 2 * i, ink);
}

void Canvas::fillRectPattern(int16_t x, int16_t y, int16_t w, int16_t h, TiPattern pattern,
                             TiInk ink) {
    const uint8_t* tile = kPatternTiles[pattern];
    for (int16_t j = 0; j < h; j++) {
        const int16_t py = y + j;
        const uint8_t row = tile[((py % 8) + 8) % 8];
        for (int16_t i = 0; i < w; i++) {
            const int16_t px = x + i;
            if (row & (0x80 >> (((px % 8) + 8) % 8))) setPixel(px, py, ink);
        }
    }
}

void Canvas::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, TiInk ink) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    hLine(x + r, y, w - 2 * r, ink);
    hLine(x + r, y + h - 1, w - 2 * r, ink);
    vLine(x, y + r, h - 2 * r, ink);
    vLine(x + w - 1, y + r, h - 2 * r, ink);

    int16_t f = 1 - r, ddFx = 1, ddFy = -2 * r, px = 0, py = r;
    while (px < py) {
        if (f >= 0) {
            py--;
            ddFy += 2;
            f += ddFy;
        }
        px++;
        ddFx += 2;
        f += ddFx;
        setPixel(x + w - r + px - 1, y + r - py, ink);
        setPixel(x + w - r + py - 1, y + r - px, ink);
        setPixel(x + w - r + px - 1, y + h - r + py - 1, ink);
        setPixel(x + w - r + py - 1, y + h - r + px - 1, ink);
        setPixel(x + r - px, y + r - py, ink);
        setPixel(x + r - py, y + r - px, ink);
        setPixel(x + r - px, y + h - r + py - 1, ink);
        setPixel(x + r - py, y + h - r + px - 1, ink);
    }
}

void Canvas::fillCircle(int16_t cx, int16_t cy, int16_t r, TiInk ink) {
    for (int16_t dy = -r; dy <= r; dy++) {
        for (int16_t dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) setPixel(cx + dx, cy + dy, ink);
        }
    }
}

void Canvas::drawCircle(int16_t cx, int16_t cy, int16_t r, TiInk ink) {
    for (int16_t dy = -r; dy <= r; dy++) {
        for (int16_t dx = -r; dx <= r; dx++) {
            const int16_t d = dx * dx + dy * dy;
            if (d <= r * r && d >= (r - 1) * (r - 1)) setPixel(cx + dx, cy + dy, ink);
        }
    }
}

void Canvas::fillTriangleUp(int16_t cx, int16_t baseY, int16_t halfWidth, int16_t height,
                            TiInk ink) {
    for (int16_t j = 0; j < height; j++) {
        const int16_t half = (int32_t)halfWidth * j / (height > 1 ? height - 1 : 1);
        hLine(cx - half, baseY - height + 1 + j, half * 2 + 1, ink);
    }
}

void Canvas::fillTriangleDown(int16_t cx, int16_t topY, int16_t halfWidth, int16_t height,
                              TiInk ink) {
    for (int16_t j = 0; j < height; j++) {
        const int16_t half =
            halfWidth - (int32_t)halfWidth * j / (height > 1 ? height - 1 : 1);
        hLine(cx - half, topY + j, half * 2 + 1, ink);
    }
}

void Canvas::drawGlyph(int16_t x, int16_t baselineY, const TiGlyph& g, const TiFont& font,
                       TiInk ink) {
    const uint8_t* bits = &font.bitmap[g.bitmapOffset];
    uint32_t bit = 0;
    for (uint8_t row = 0; row < g.height; row++) {
        for (uint8_t col = 0; col < g.width; col++, bit++) {
            if (bits[bit >> 3] & (0x80 >> (bit & 7))) {
                setPixel(x + g.xOffset + col, baselineY + g.yOffset + row, ink);
            }
        }
    }
}

int16_t Canvas::drawText(int16_t x, int16_t baselineY, const char* text, const TiFont& font,
                         TiInk ink) {
    return drawTextTracked(x, baselineY, text, font, 0, ink);
}

int16_t Canvas::drawTextTracked(int16_t x, int16_t baselineY, const char* text, const TiFont& font,
                                int16_t spacing, TiInk ink) {
    const int16_t startX = x;
    for (const char* p = text; *p; p++) {
        const uint8_t c = (uint8_t)*p;
        if (c < font.first || c > font.last) continue;
        const TiGlyph& g = font.glyph[c - font.first];
        drawGlyph(x, baselineY, g, font, ink);
        x += g.xAdvance + spacing;
    }
    return x - startX;
}

int16_t Canvas::drawTextAligned(int16_t x, int16_t baselineY, const char* text, const TiFont& font,
                                TiAlign align, TiInk ink) {
    const int16_t w = textWidth(text, font);
    int16_t left = x;
    if (align == kCenter) left = x - w / 2;
    if (align == kRight) left = x - w;
    return drawText(left, baselineY, text, font, ink);
}

int16_t Canvas::textWidth(const char* text, const TiFont& font, int16_t spacing) {
    int16_t w = 0;
    for (const char* p = text; *p; p++) {
        const uint8_t c = (uint8_t)*p;
        if (c < font.first || c > font.last) continue;
        w += font.glyph[c - font.first].xAdvance + spacing;
    }
    if (spacing && w) w -= spacing;  // no trailing gap
    return w;
}
