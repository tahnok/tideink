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

// This is as deep as the clock can usefully sleep. The reasoning is worth
// recording, because the two obvious next moves -- esp_sleep_pd_config() to
// force domains off by hand, and dropping the RTC memory to "hibernate" and
// boot fresh each time -- both look like free current and are not.
//
//   RTC fast memory   Stays on, and would stay on even if this firmware kept
//                     nothing in it: ESP-IDF turns the AUTO default into ON
//                     unconditionally, so that the deep-sleep stub has somewhere
//                     to run (get_power_down_flags() in sleep_modes.c). The
//                     cached predictions ride along in a domain that is powered
//                     either way. Forcing it off is possible, but the C3
//                     datasheet publishes exactly one deep-sleep figure -- 5 uA,
//                     "RTC timer + RTC memory", measured with the memory powered
//                     -- and no hibernation figure at all. The next row down is
//                     the chip switched off at 1 uA, which cannot wake on a
//                     timer. So the whole prize is under 4 uA, and in truth much
//                     less: the RTC timer, the PMU and the RTC watchdog stay up
//                     either way.
//   RTC peripherals   Already off. The C3 does not define
//                     SOC_PM_SUPPORT_RTC_PERIPH_PD at all, so IDF never has a
//                     reason to hold the domain up and flags it down on every
//                     sleep. Deep-sleep GPIO wakeup still works: on this chip it
//                     runs off the always-on VDD3P3_RTC pads, not that domain.
//   RTC 8 MHz osc     Already off. It is only held up when the slow clock is
//                     derived from it, and this board's slow clock is the
//                     150 kHz RC oscillator.
//   XTAL, CPU, flash  Already off, unconditionally, in deep sleep.
//
// The one state genuinely below this is ultra-low-power deep sleep, and it is
// the wake button that rules it out rather than the cache: IDF sets
// RTC_SLEEP_NO_ULTRA_LOW unless ultra-low is explicitly enabled, and in
// ultra-low an RTC IO cannot be used as an input at all.
//
// So the retained memory is not buying the chip anything -- it buys the *panel*.
// Every scheduled wake brings the radio up for NTP and the download anyway, so a
// good day runs identically either way. It is the bad days that differ: see
// drawnScreenId() in main.cpp, which is what keeps yesterday's still-valid
// predictions on screen through a failed refresh instead of a placeholder, and
// what stops an outage's hourly retries from repainting all 800x480 every hour.
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
