#include "power.h"

#include <Arduino.h>
#include <HWCDC.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "config.h"

namespace {

#if ENABLE_BATTERY_LATCH
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

bool usbAttached() {
    // HWCDC watches for start-of-frame packets on a FreeRTOS tick hook and
    // clears the flag after ~5 ms without one. It is initialised to true, so
    // this is only meaningful once the scheduler has run for a few ticks --
    // fine here, since the caller asks at the end of a wake cycle.
    return HWCDC::isPlugged();
}

int64_t stayAwakeFor(int64_t seconds, int64_t capSeconds) {
    if (capSeconds > 0 && seconds > capSeconds) seconds = capSeconds;
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

// The deep sleep below is as deep as this board goes, which is worth spelling
// out because the temptation is to reach for esp_sleep_pd_config() and force
// domains off by hand. On the C3 that is at best a no-op and at worst a way to
// lose the cache:
//
//   RTC fast memory   stays on, and has to -- it holds the tide predictions and
//                     the wake target that make a redraw free. About 8 kB of
//                     retained SRAM, a fraction of a microamp.
//   RTC peripherals   already off. ESP-IDF only keeps this domain up for touch,
//                     ULP and ext0 wakeups, none of which the C3 has or this
//                     firmware arms. Deep-sleep GPIO wakeup runs off the
//                     always-on VDD3P3_RTC pads instead, so the button costs
//                     nothing here.
//   RTC 8 MHz osc     already off. It is only held up when the slow clock is
//                     derived from it; this board's slow clock is the 150 kHz
//                     RC oscillator.
//   XTAL, CPU, flash  already off, unconditionally, in deep sleep.
//
// Everything above is what ESP_PD_OPTION_AUTO -- the default every domain is
// left at -- already resolves to, so there is nothing left to switch off. What
// remains is the RTC timer, the retained memory, and the two pads held below.
// Hibernation, the one mode deeper than this, drops the retained memory: the
// clock would come back knowing nothing, download on every wake, and spend more
// energy on radio than it saved on leakage.
//
// The board's own draw is a separate matter and dwarfs all of it -- see the
// battery-sense divider note in docs/hardware.md.
void deepSleepFor(int64_t seconds) {
    if (seconds < 60) seconds = 60;
    Serial.printf("[power] sleeping for %lld s\n", (long long)seconds);
    Serial.flush();

    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
#if ENABLE_BATTERY_LATCH
    // The GPIO matrix powers down in deep sleep, so the latch has to be pinned
    // by the RTC hold or the board switches itself off as soon as it sleeps.
    pinMode(PIN_BATTERY_LATCH, OUTPUT);
    digitalWrite(PIN_BATTERY_LATCH, BATTERY_LATCH_ACTIVE_LEVEL);
    gpio_hold_en((gpio_num_t)PIN_BATTERY_LATCH);
    gpio_deep_sleep_hold_en();
#endif
#if ENABLE_BUTTON_WAKE
    // Re-assert the pull immediately before arming the wake source. powerBegin()
    // set it, but a radio session and a panel refresh have run since; a floating
    // pin here wakes the device straight back up.
    pinMode(PIN_WAKE_BUTTON, WAKE_BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    // The ESP32-C3 can wake from deep sleep on any GPIO 0-5.
    esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_WAKE_BUTTON,
                                      WAKE_BUTTON_ACTIVE_LOW ? ESP_GPIO_WAKEUP_GPIO_LOW
                                                             : ESP_GPIO_WAKEUP_GPIO_HIGH);
#endif
    esp_deep_sleep_start();
}
