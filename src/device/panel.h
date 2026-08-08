// Thin wrapper over the X4's e-paper panel: hand it a Canvas, get a refreshed
// screen and a display back in deep sleep.
#pragma once

#include "canvas.h"

// Framebuffer for the whole 800x480 screen, 48 kB. Shared with the renderer so
// only one copy exists on the device.
uint8_t* panelFramebuffer();

void panelBegin();
// Pushes the framebuffer with a full refresh (the only sensible mode after a
// deep sleep) and then hibernates the controller.
void panelShow(const Canvas& canvas);
