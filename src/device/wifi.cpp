#include "wifi.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "secrets.h"

bool connectWifi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    const uint32_t deadline = millis() + WIFI_TIMEOUT_MS;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
        delay(150);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[net] Wi-Fi connect failed");
        return false;
    }
    Serial.printf("[net] connected, %d dBm, ip %s\n", WiFi.RSSI(),
                  WiFi.localIP().toString().c_str());
    return true;
}

void stopWifi() {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
}
