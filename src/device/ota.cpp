#include "ota.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

#include "canvas.h"
#include "config.h"
#include "panel.h"
#include "render.h"
#include "wifi.h"

bool otaButtonHeld() {
    int raw = analogRead(PIN_BUTTON_ADC1);
    return abs(raw - OTA_BUTTON_RAW) <= OTA_BUTTON_TOLERANCE;
}

void otaEnter() {
    if (!connectWifi()) {
        Canvas canvas(panelFramebuffer(), kScreenWidth, kScreenHeight);
        renderMessageScreen(canvas, "OTA Update", "Wi-Fi failed.", "Check secrets.h.");
        panelShow(canvas);
        delay(3000);
        ESP.restart();
    }

    ArduinoOTA.setHostname(OTA_HOSTNAME);

    ArduinoOTA.onStart([]() { Serial.println("[ota] start"); });
    ArduinoOTA.onEnd([]() { Serial.println("\n[ota] done"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[ota] %u%%\r", progress * 100 / total);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[ota] error %u: ", error);
        switch (error) {
            case OTA_AUTH_ERROR: Serial.println("auth"); break;
            case OTA_BEGIN_ERROR: Serial.println("begin"); break;
            case OTA_CONNECT_ERROR: Serial.println("connect"); break;
            case OTA_RECEIVE_ERROR: Serial.println("receive"); break;
            case OTA_END_ERROR: Serial.println("end"); break;
        }
    });

    ArduinoOTA.begin();

    char line1[64];
    snprintf(line1, sizeof(line1), "%s / %s.local", WiFi.localIP().toString().c_str(), OTA_HOSTNAME);

    Canvas canvas(panelFramebuffer(), kScreenWidth, kScreenHeight);
    renderMessageScreen(canvas, "OTA Update", line1, "Waiting for firmware...");
    panelShow(canvas);

    Serial.printf("[ota] listening at %s\n", line1);

    const uint32_t start = millis();
    while (millis() - start < OTA_TIMEOUT_MS) {
        ArduinoOTA.handle();
        delay(10);
    }

    Serial.println("[ota] timed out, restarting");
    stopWifi();
    ESP.restart();
}
