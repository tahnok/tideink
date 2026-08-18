#include "power.h"

#include <Arduino.h>
#include <HWCDC.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "config.h"

namespace {

#if ENABLE_BATTERY_LATCH
// Set by powerOff() once the latch has been let go, so the deep sleep it falls
// back to does not helpfully turn the board back on again.
bool g_latchReleased = false;

// Assert the battery latch and keep it asserted across deep sleep.
//
// Order matters. While a pad is held, configuration writes are staged but do
// not reach it, so the pin is driven to the right level *first* and the hold
// released after -- releasing first would let the pad float for the microseconds
// before it is reconfigured, and that is long enough to drop the rail the code
// is running from.
void latchBattery() {
    pinMode(PIN_BATTERY_LATCH, OUTPUT);
    digitalWrite(PIN_BATTERY_LATCH, BATTERY_LATCH_ACTIVE_LEVEL);
    gpio_hold_dis((gpio_num_t)PIN_BATTERY_LATCH);
    pinMode(PIN_BATTERY_LATCH, OUTPUT);
    digitalWrite(PIN_BATTERY_LATCH, BATTERY_LATCH_ACTIVE_LEVEL);
}
#endif

// Rest-voltage curve for a 1S LiPo, one entry per 10%. Taken from the FreeInk
// SDK's BatteryMonitor, which is the battery code CrossPoint runs on this exact
// board, so it is the one curve here that has been checked against the hardware
// rather than assumed.
//
// The shape is the point. A LiPo spends most of its charge between 3.68 V and
// 4.06 V and then falls off a cliff, so the straight line from "empty" to "full"
// this replaced read 44% at a cell that actually had 10% left in it. On a clock
// that is supposed to warn you before it stops, that is the one error that
// matters.
//
// 0% is anchored at 3.45 V rather than the cell's protection cut-off: below that
// the remaining runtime is minutes, and it leaves headroom for the sag under an
// e-paper refresh, the heaviest load the board draws.
const uint16_t kLipoCurveMv[] = {
    3450,  //   0%
    3680,  //  10%
    3740,  //  20%
    3770,  //  30%
    3790,  //  40%
    3820,  //  50%
    3870,  //  60%
    3920,  //  70%
    3980,  //  80%
    4060,  //  90%
    4200,  // 100%
};
const uint8_t kLipoCurvePoints = sizeof(kLipoCurveMv) / sizeof(kLipoCurveMv[0]);

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
#if ENABLE_BATTERY_LATCH
    // First thing on every boot: without this the clock only runs on the cable.
    latchBattery();
#endif
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
#if ENABLE_CHARGE_DETECT
    pinMode(PIN_CHARGE_DETECT, INPUT);
#endif
#if ENABLE_BUTTON_WAKE
    pinMode(PIN_WAKE_BUTTON, WAKE_BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
#endif
#if ENABLE_OTA_WAKE
    pinMode(PIN_BUTTON_ADC1, INPUT);
#endif
}

uint16_t batteryMillivolts() { return readMillivoltsAveraged(); }

int16_t batteryPercent() {
    const uint16_t mv = batteryMillivolts();
    if (mv < 500) return -1;  // nothing plausible on the divider
    if (mv <= kLipoCurveMv[0]) return 0;
    if (mv >= kLipoCurveMv[kLipoCurvePoints - 1]) return 100;
    for (uint8_t i = 1; i < kLipoCurvePoints; i++) {
        if (mv < kLipoCurveMv[i]) {
            // Interpolated inside the notch rather than snapped to it, so the
            // readout still moves as the cell drains instead of standing still
            // for a week and then dropping ten points.
            const uint16_t lo = kLipoCurveMv[i - 1], hi = kLipoCurveMv[i];
            return (int16_t)((i - 1) * 10 + (int32_t)(mv - lo) * 10 / (hi - lo));
        }
    }
    return 100;
}

bool batteryCritical(uint16_t* mv) {
#if ENABLE_LOW_BATTERY_SHUTDOWN
    uint16_t reading = batteryMillivolts();
    if (reading < BATTERY_CRITICAL_MV) {
        // A cell that has just driven a panel refresh or a radio session reads
        // low for a moment and then recovers. Give it that moment before
        // writing the battery off.
        delay(BATTERY_CRITICAL_CONFIRM_MS);
        reading = batteryMillivolts();
    }
    if (mv) *mv = reading;
    // Under 500 mV is nothing plausible on the divider -- an unpopulated cell or
    // a bad read, not a flat battery. Switching off on that would strand a board
    // whose only fault is a sense line.
    return reading >= 500 && reading < BATTERY_CRITICAL_MV;
#else
    if (mv) *mv = batteryMillivolts();
    return false;
#endif
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

bool usbAttached() {
    // HWCDC watches for start-of-frame packets on a FreeRTOS tick hook and
    // clears the flag after ~5 ms without one. It is initialised to true, so
    // this is only meaningful once the scheduler has run for a few ticks --
    // fine here, since the caller asks at the end of a wake cycle.
    return HWCDC::isPlugged();
}

int64_t stayAwakeFor(int64_t seconds) {
    if (seconds > (int64_t)MAX_SLEEP_MINUTES * 60) seconds = (int64_t)MAX_SLEEP_MINUTES * 60;
    if (seconds < 0) seconds = 0;

    Serial.printf("[power] USB attached, staying awake %lld s instead of sleeping\n",
                  (long long)seconds);
    Serial.flush();

    // Unsigned arithmetic on the start offset, so a millis() rollover during
    // the window still compares correctly.
    const uint32_t start = millis();
    const uint32_t windowMs = (uint32_t)seconds * 1000u;
    for (;;) {
        const uint32_t elapsed = millis() - start;
        if (elapsed >= windowMs) return 0;
        if (!usbAttached()) {
            const int64_t left = (windowMs - elapsed + 999) / 1000;
            Serial.printf("[power] USB gone, sleeping for the remaining %lld s\n",
                          (long long)left);
            return left > 0 ? left : 1;
        }
        delay(250);
    }
}

void deepSleepFor(int64_t seconds) {
    if (seconds < 60) seconds = 60;
    Serial.printf("[power] sleeping for %lld s\n", (long long)seconds);
    Serial.flush();

    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
#if ENABLE_BATTERY_LATCH
    // The GPIO matrix powers down in deep sleep, so the latch has to be pinned
    // by the RTC hold or the board switches itself off as soon as it sleeps.
    // After powerOff() that is exactly what should happen, so the release it
    // already pinned is left alone rather than undone here.
    if (!g_latchReleased) {
        pinMode(PIN_BATTERY_LATCH, OUTPUT);
        digitalWrite(PIN_BATTERY_LATCH, BATTERY_LATCH_ACTIVE_LEVEL);
        gpio_hold_en((gpio_num_t)PIN_BATTERY_LATCH);
    }
    gpio_deep_sleep_hold_en();
#endif
#if ENABLE_BUTTON_WAKE
    // Re-assert the pull immediately before arming the wake source. powerBegin()
    // set it, but a radio session and a panel refresh have run since; a floating
    // pin here wakes the device straight back up.
    pinMode(PIN_WAKE_BUTTON, WAKE_BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    uint64_t wakeMask = 1ULL << PIN_WAKE_BUTTON;
#if ENABLE_OTA_WAKE
    pinMode(PIN_BUTTON_ADC1, INPUT);
    wakeMask |= 1ULL << PIN_BUTTON_ADC1;
#endif
    // The ESP32-C3 can wake from deep sleep on any GPIO 0-5.
    esp_deep_sleep_enable_gpio_wakeup(wakeMask,
                                      WAKE_BUTTON_ACTIVE_LOW ? ESP_GPIO_WAKEUP_GPIO_LOW
                                                             : ESP_GPIO_WAKEUP_GPIO_HIGH);
#endif
    esp_deep_sleep_start();
}

void powerOff() {
#if ENABLE_BATTERY_LATCH
    Serial.println("[power] battery critical, releasing the latch");
    Serial.flush();

    // Mirror of latchBattery(), in reverse. The hold has to come off before the
    // pad will accept a new level, and the level is pinned again straight after
    // so the MOSFET gate is driven low through deep sleep rather than floating.
    gpio_hold_dis((gpio_num_t)PIN_BATTERY_LATCH);
    pinMode(PIN_BATTERY_LATCH, OUTPUT);
    digitalWrite(PIN_BATTERY_LATCH, !BATTERY_LATCH_ACTIVE_LEVEL);
    gpio_hold_en((gpio_num_t)PIN_BATTERY_LATCH);
    g_latchReleased = true;

    // On battery the rail is gone well inside this, and nothing below runs.
    delay(2000);
    Serial.println("[power] still running, so the cable is holding the rail up");
#endif
    // Only reached on USB (or with the latch compiled out). Sleep on the timer
    // and let the next boot decide again: by then the cell may have charged, and
    // if the cable comes out first the released latch finishes the job.
    deepSleepFor((int64_t)LOW_BATTERY_RECHECK_MINUTES * 60);
}
