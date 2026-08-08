#include "power.h"

#include <Arduino.h>
#include <esp_sleep.h>

#include "config.h"

namespace {

uint16_t readMillivoltsAveraged() {
    uint32_t total = 0;
    const int samples = 8;
    for (int i = 0; i < samples; i++) {
        total += analogReadMilliVolts(PIN_BATTERY_ADC);
        delay(2);
    }
    return (uint16_t)((total / samples) * BATTERY_DIVIDER);
}

}  // namespace

void powerBegin() {
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
#if ENABLE_CHARGE_DETECT
    pinMode(PIN_CHARGE_DETECT, INPUT);
#endif
#if ENABLE_BUTTON_WAKE
    pinMode(PIN_WAKE_BUTTON, WAKE_BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
#endif
}

uint16_t batteryMillivolts() { return readMillivoltsAveraged(); }

int16_t batteryPercent() {
    const uint16_t mv = batteryMillivolts();
    if (mv < 500) return -1;  // nothing plausible on the divider
    if (mv <= BATTERY_EMPTY_MV) return 0;
    if (mv >= BATTERY_FULL_MV) return 100;
    return (int16_t)((int32_t)(mv - BATTERY_EMPTY_MV) * 100 /
                     (BATTERY_FULL_MV - BATTERY_EMPTY_MV));
}

bool batteryCharging() {
#if ENABLE_CHARGE_DETECT
    return digitalRead(PIN_CHARGE_DETECT) == CHARGE_ACTIVE_LEVEL;
#else
    return false;
#endif
}

bool wokeFromButton() {
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
}

void deepSleepFor(int64_t seconds) {
    if (seconds < 60) seconds = 60;
    Serial.printf("[power] sleeping for %lld s\n", (long long)seconds);
    Serial.flush();

    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
#if ENABLE_BUTTON_WAKE
    // The ESP32-C3 can wake from deep sleep on any GPIO 0-5.
    esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_WAKE_BUTTON,
                                      WAKE_BUTTON_ACTIVE_LOW ? ESP_GPIO_WAKEUP_GPIO_LOW
                                                             : ESP_GPIO_WAKEUP_GPIO_HIGH);
#endif
    esp_deep_sleep_start();
}
