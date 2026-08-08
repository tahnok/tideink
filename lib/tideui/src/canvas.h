// A tiny 1-bit-per-pixel drawing surface.
//
// Everything the tide clock draws goes through this class, on the device and in
// the host simulator alike, so a screenshot render is pixel-identical to what
// the e-paper panel shows.
#pragma once

#include <stdint.h>

#include "font.h"

// Ink values. The framebuffer stores 1 = black, MSB first, row-major.
enum TiInk : uint8_t {
    kWhite = 0,
    kBlack = 1,
};

// 8x8 dither patterns for the shaded areas an e-paper panel renders nicely.
enum TiPattern : uint8_t {
    kSolid,
    kCheck50,     // 50% checkerboard
    kDots25,      // 25% coverage
    kDots12,      // 12.5% coverage
    kHatchRight,  // diagonal hatch
};

enum TiAlign : uint8_t {
    kLeft,
    kCenter,
    kRight,
};

class Canvas {
   public:
    // `buffer` must hold stride() * height bytes and outlives the Canvas.
    Canvas(uint8_t* buffer, int16_t width, int16_t height);

    int16_t width() const { return width_; }
    int16_t height() const { return height_; }
    int16_t stride() const { return stride_; }
    const uint8_t* buffer() const { return buffer_; }
    uint8_t* buffer() { return buffer_; }
    uint32_t sizeBytes() const { return (uint32_t)stride_ * height_; }

    void clear(TiInk ink = kWhite);
    void setPixel(int16_t x, int16_t y, TiInk ink);
    TiInk pixel(int16_t x, int16_t y) const;

    void hLine(int16_t x, int16_t y, int16_t w, TiInk ink = kBlack);
    void vLine(int16_t x, int16_t y, int16_t h, TiInk ink = kBlack);
    void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, TiInk ink = kBlack);
    // Vertical dashed rule, `on` pixels drawn then `off` pixels skipped.
    void dashedVLine(int16_t x, int16_t y, int16_t h, uint8_t on, uint8_t off, TiInk ink = kBlack);
    void dashedHLine(int16_t x, int16_t y, int16_t w, uint8_t on, uint8_t off, TiInk ink = kBlack);

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, TiInk ink = kBlack);
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, TiInk ink = kBlack);
    // Rectangle with `t` pixels of border thickness.
    void drawRectThick(int16_t x, int16_t y, int16_t w, int16_t h, int16_t t, TiInk ink = kBlack);
    void fillRectPattern(int16_t x, int16_t y, int16_t w, int16_t h, TiPattern pattern,
                         TiInk ink = kBlack);
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, TiInk ink = kBlack);
    void fillCircle(int16_t cx, int16_t cy, int16_t r, TiInk ink = kBlack);
    void drawCircle(int16_t cx, int16_t cy, int16_t r, TiInk ink = kBlack);
    // Solid upward ("high") or downward ("low") triangle, used as the tide arrow.
    void fillTriangleUp(int16_t cx, int16_t baseY, int16_t halfWidth, int16_t height,
                        TiInk ink = kBlack);
    void fillTriangleDown(int16_t cx, int16_t topY, int16_t halfWidth, int16_t height,
                          TiInk ink = kBlack);

    // Text is positioned by its baseline. Returns the advance width drawn.
    int16_t drawText(int16_t x, int16_t baselineY, const char* text, const TiFont& font,
                     TiInk ink = kBlack);
    int16_t drawTextAligned(int16_t x, int16_t baselineY, const char* text, const TiFont& font,
                            TiAlign align, TiInk ink = kBlack);
    // Draws with `spacing` extra pixels between glyphs (small-caps style labels).
    int16_t drawTextTracked(int16_t x, int16_t baselineY, const char* text, const TiFont& font,
                            int16_t spacing, TiInk ink = kBlack);

    static int16_t textWidth(const char* text, const TiFont& font, int16_t spacing = 0);

   private:
    void drawGlyph(int16_t x, int16_t baselineY, const TiGlyph& g, const TiFont& font, TiInk ink);

    uint8_t* buffer_;
    int16_t width_;
    int16_t height_;
    int16_t stride_;
};
