// Over-the-air firmware update mode, triggered by the RIGHT button.
#pragma once

// True when the RIGHT button on the GPIO1 ladder is held (pulling the pin to
// near-ground). Called early in setup() to decide whether to enter OTA mode.
bool otaButtonHeld();

// Connects to Wi-Fi, starts ArduinoOTA, shows the IP on the panel, and waits
// for a firmware push. Restarts on success (ArduinoOTA does this) or after
// OTA_TIMEOUT_MS with no update. Does not return.
void otaEnter();
