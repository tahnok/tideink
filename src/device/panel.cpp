#include "panel.h"

#include <Arduino.h>
#include <SPI.h>
#include <gdeq/GxEPD2_426_GDEQ0426T82.h>

#include "config.h"
#include "render.h"

namespace {

// The panel driver is used directly rather than through GxEPD2_BW: we already
// hold a full-screen framebuffer, and going through the graphics wrapper would
// allocate a second 48 kB copy.
GxEPD2_426_GDEQ0426T82 g_epd(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY);

uint8_t g_framebuffer[(kScreenWidth / 8) * kScreenHeight];

}  // namespace

uint8_t* panelFramebuffer() { return g_framebuffer; }

void panelBegin() {
    SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
    // initial=false: the panel keeps its image through deep sleep, so there is
    // no need to blank it on every wake.
    g_epd.init(115200, false, 2, false);
}

void panelShow(const Canvas& canvas) {
    // Our framebuffer uses 1 = black ink; the controller expects 1 = white, so
    // the data is inverted on the way out.
    g_epd.writeImageForFullRefresh(canvas.buffer(), 0, 0, canvas.width(), canvas.height(),
                                   /*invert=*/true);
    g_epd.refresh(false);
    g_epd.powerOff();
    g_epd.hibernate();
}
