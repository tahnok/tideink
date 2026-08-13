// Hardware probe for the Xteink X4. Not part of the tide clock -- this is the
// firmware that produced the numbers in docs/hardware.md, kept so the claims
// there can be re-checked on another unit.
//
//   pio run -e diag -t upload && pio device monitor
//
// It prints a static report (chip, clocks, pin levels, I2C scan), then drops
// into a loop that names every button press. Single-key serial commands:
//
//   b   re-read the battery divider
//   i   re-scan I2C
//   s   deep sleep 20 s on the timer, to time the RTC slow clock
//   g   deep sleep until the power button is pressed
//   p   dump every readable GPIO level
//   u   re-probe GPIO20
//   w   watch GPIO20 for 60 s across a cable unplug/replug
//   h   dump the boot history held in flash;  H  clear it
//   L   drive GPIO13 HIGH, latch it, then deep sleep 300 s (power-hold test)

#include <Arduino.h>
#include <HWCDC.h>
#include <Preferences.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_sleep.h>
#include <soc/rtc.h>

extern "C" {
#include <esp_private/esp_clk.h>
}

// Documented pin map, from the Adafruit CircuitPython guide and the open-x4
// sample firmware. Everything here is what the probe is trying to confirm.
static const int kPinBatteryAdc = 0;
static const int kPinButtonAdc1 = 1;  // back / confirm / left / right
static const int kPinButtonAdc2 = 2;  // up / down
static const int kPinPowerButton = 3;
static const int kPinUsbDetect = 20;

static const int kPinI2cSda = 20;  // only if this is an X3-style board
static const int kPinI2cScl = 0;

// Battery latch MOSFET: held HIGH the board runs off the cell, released or
// driven LOW it powers off the instant USB goes away. CrossPoint drives this
// low on purpose to shut the reader down; a clock needs it high.
static const int kPinBatteryLatch = 13;

static void latchBattery() {
    pinMode(kPinBatteryLatch, OUTPUT);
    digitalWrite(kPinBatteryLatch, HIGH);
    gpio_hold_dis((gpio_num_t)kPinBatteryLatch);
    pinMode(kPinBatteryLatch, OUTPUT);
    digitalWrite(kPinBatteryLatch, HIGH);
}

RTC_DATA_ATTR uint32_t g_bootCount;
RTC_DATA_ATTR uint32_t g_rtcCanary;
RTC_DATA_ATTR int64_t g_sleepStartedUs;
RTC_DATA_ATTR uint32_t g_requestedSleepMs;

static const uint32_t kCanary = 0xD1A6C0DEuL;

// ------------------------------------------------------------- boot history ---
// RTC memory cannot answer "did the board lose power?": .rtc.data is
// re-initialised on every reset that is not a deep-sleep wake, so a cold canary
// proves nothing. Attaching a serial terminal also resets the chip through the
// USB Serial/JTAG DTR/RTS lines, which means the moment of interest cannot be
// observed live. NVS survives both, so each boot appends a record here and the
// history is read back later.

static const char* resetReasonName(esp_reset_reason_t r);
static const char* wakeCauseName(esp_sleep_wakeup_cause_t c);
static uint32_t readAdcMv(int pin, int samples);

static Preferences g_prefs;

struct BootRecord {
    uint8_t reset;
    uint8_t wake;
    // GPIO20 sampled at wake, before anything else touches it. A timer wake
    // recorded with USB absent is direct proof the board ran off the battery --
    // no arithmetic about elapsed time required.
    uint8_t usb;
    // Cell millivolts at wake. While plugged in the divider reads the charger,
    // not the cell, and the X4 has no fuel gauge to ask instead -- so the only
    // view of the battery's real state is this number on the wakes where USB is
    // absent. A cell that collapses across those wakes is a dying cell, which
    // looks nothing like a power rail being switched off.
    uint16_t mv;
};

static const int kHistoryMax = 24;

static void historyAppend() {
    g_prefs.begin("x4diag", false);
    size_t len = g_prefs.getBytesLength("hist");
    BootRecord recs[kHistoryMax];
    int n = 0;
    if (len > 0 && len <= sizeof(recs)) {
        g_prefs.getBytes("hist", recs, len);
        n = len / sizeof(BootRecord);
    }
    if (n >= kHistoryMax) {
        memmove(recs, recs + 1, (kHistoryMax - 1) * sizeof(BootRecord));
        n = kHistoryMax - 1;
    }
    recs[n].reset = (uint8_t)esp_reset_reason();
    recs[n].wake = (uint8_t)esp_sleep_get_wakeup_cause();
    pinMode(kPinUsbDetect, INPUT);
    recs[n].usb = (uint8_t)digitalRead(kPinUsbDetect);
    analogSetPinAttenuation(kPinBatteryAdc, ADC_11db);
    recs[n].mv = (uint16_t)(readAdcMv(kPinBatteryAdc, 8) * 2);
    n++;
    g_prefs.putBytes("hist", recs, n * sizeof(BootRecord));

    // Uncapped tallies. The ring above loses its oldest entries, and without a
    // full count there is no way to tell "slept through the unplug" from "died
    // and came back": both end in a power-on. Comparing timer wakes against the
    // host's elapsed wall clock does distinguish them, because a board that
    // lost power also lost that many minutes of wakes.
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        g_prefs.putUInt("nTimer", g_prefs.getUInt("nTimer", 0) + 1);
    } else if (esp_reset_reason() == ESP_RST_POWERON) {
        g_prefs.putUInt("nPowerOn", g_prefs.getUInt("nPowerOn", 0) + 1);
    }
    g_prefs.end();
}

static void historyDump() {
    g_prefs.begin("x4diag", true);
    size_t len = g_prefs.getBytesLength("hist");
    BootRecord recs[kHistoryMax];
    int n = 0;
    if (len > 0 && len <= sizeof(recs)) {
        g_prefs.getBytes("hist", recs, len);
        n = len / sizeof(BootRecord);
    }
    g_prefs.end();

    Serial.println("--- boot history (from flash, oldest first) ---");
    int unpluggedWakes = 0;
    for (int i = 0; i < n; i++) {
        const bool onBattery = recs[i].usb == 0;
        if (onBattery && recs[i].wake == ESP_SLEEP_WAKEUP_TIMER) unpluggedWakes++;
        Serial.printf("#%-2d reset=%-16s wake=%-22s usb=%-7s cell=%u mV%s\n", i + 1,
                      resetReasonName((esp_reset_reason_t)recs[i].reset),
                      wakeCauseName((esp_sleep_wakeup_cause_t)recs[i].wake),
                      recs[i].usb ? "present" : "ABSENT", recs[i].mv,
                      (onBattery && recs[i].wake == ESP_SLEEP_WAKEUP_TIMER) ? "  <- ran on battery"
                                                                           : "");
    }
    Serial.printf("VERDICT %d timer wake(s) with USB absent\n", unpluggedWakes);
    g_prefs.begin("x4diag", true);
    const uint32_t nTimer = g_prefs.getUInt("nTimer", 0);
    const uint32_t nPowerOn = g_prefs.getUInt("nPowerOn", 0);
    g_prefs.end();

    Serial.printf("%d record(s) in the ring (oldest are dropped past %d).\n", n, kHistoryMax);
    Serial.printf("TALLY timer=%u poweron=%u\n", nTimer, nPowerOn);
}

// Repeat mode removes the stopwatch from the unplug test: the board wakes on a
// short timer over and over, writing a record each time, so the cable can be
// pulled at any moment and the history still shows what happened. A run of
// timer wakes across the gap means it stayed alive; a power-on entry in the
// middle means the rail dropped.
static bool repeatEnabled() {
    g_prefs.begin("x4diag", true);
    const bool on = g_prefs.getBool("repeat", false);
    g_prefs.end();
    return on;
}

static void repeatSet(bool on) {
    g_prefs.begin("x4diag", false);
    g_prefs.putBool("repeat", on);
    g_prefs.end();
}

// The board dies within a minute of the cable coming out, so it never survives
// long enough to log a battery reading on a wake. This samples the cell while
// still awake, from the instant GPIO20 drops, and commits each sample to flash
// as it goes -- so the trace survives whatever kills the board.
//
// A healthy cell settles near 3.9-4.05 V and plateaus under this ~30 mA awake
// load. A cell that is flat, disconnected or high-impedance collapses toward
// the brownout threshold in seconds, and the difference is unmistakable.
static void decayTest(uint32_t maxSeconds) {
    g_prefs.begin("x4decay", false);
    g_prefs.putUInt("n", 0);
    g_prefs.end();

    pinMode(kPinUsbDetect, INPUT);
    analogSetPinAttenuation(kPinBatteryAdc, ADC_11db);

    Serial.println("[decay] waiting for the cable to come out...");
    Serial.flush();
    while (digitalRead(kPinUsbDetect) != LOW) delay(50);

    const uint32_t t0 = millis();
    Serial.println("[decay] cable out, sampling the cell every second");
    Serial.flush();

    uint32_t n = 0;
    while (n < maxSeconds) {
        const uint32_t mv = readAdcMv(kPinBatteryAdc, 4) * 2;
        const uint32_t t = millis() - t0;

        char key[8];
        snprintf(key, sizeof(key), "s%lu", (unsigned long)n);
        g_prefs.begin("x4decay", false);
        // centiseconds in the high half so a long run cannot overflow 16 bits
        g_prefs.putUInt(key, ((t / 10) << 16) | (mv & 0xFFFF));
        g_prefs.putUInt("n", n + 1);
        g_prefs.end();

        Serial.printf("[decay] t+%-6u ms  cell %u mV  usb=%d\n", t, mv,
                      digitalRead(kPinUsbDetect));
        n++;
        if (digitalRead(kPinUsbDetect) == HIGH) {
            Serial.println("[decay] cable back, stopping");
            break;
        }
        delay(1000);
    }
    Serial.println("[decay] done -- press 'd' to read the trace back");
}

static void decayDump() {
    g_prefs.begin("x4decay", true);
    const uint32_t n = g_prefs.getUInt("n", 0);
    Serial.println("--- battery decay after the cable was pulled ---");
    for (uint32_t i = 0; i < n; i++) {
        char key[8];
        snprintf(key, sizeof(key), "s%lu", (unsigned long)i);
        const uint32_t packed = g_prefs.getUInt(key, 0);
        Serial.printf("t+%-7.2f s  cell %u mV\n", (packed >> 16) / 100.0, packed & 0xFFFF);
    }
    g_prefs.end();
    Serial.printf("%u sample(s)\n", n);
}

static void historyClear() {
    g_prefs.begin("x4diag", false);
    g_prefs.remove("hist");
    g_prefs.putUInt("nTimer", 0);
    g_prefs.putUInt("nPowerOn", 0);
    g_prefs.end();
    Serial.println("[history] cleared");
}

// ------------------------------------------------------------------ report ---

static const char* resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_EXT: return "external pin";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "task watchdog";
        case ESP_RST_WDT: return "other watchdog";
        case ESP_RST_DEEPSLEEP: return "deep sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown";
    }
}

static const char* wakeCauseName(esp_sleep_wakeup_cause_t c) {
    switch (c) {
        case ESP_SLEEP_WAKEUP_UNDEFINED: return "not a deep-sleep wake";
        case ESP_SLEEP_WAKEUP_TIMER: return "timer";
        case ESP_SLEEP_WAKEUP_GPIO: return "gpio";
        case ESP_SLEEP_WAKEUP_UART: return "uart";
        default: return "other";
    }
}

static void reportChip() {
    esp_chip_info_t info;
    esp_chip_info(&info);
    uint32_t flash = 0;
    esp_flash_get_size(esp_flash_default_chip, &flash);

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    Serial.println("--- chip ---");
    Serial.printf("model            ESP32-C3 (chip model %d), revision %d, %d core\n",
                  (int)info.model, (int)info.revision, (int)info.cores);
    Serial.printf("features         wifi=%d ble=%d embedded-flash=%d\n",
                  (info.features & CHIP_FEATURE_WIFI_BGN) ? 1 : 0,
                  (info.features & CHIP_FEATURE_BLE) ? 1 : 0,
                  (info.features & CHIP_FEATURE_EMB_FLASH) ? 1 : 0);
    Serial.printf("flash            %u bytes (%u MB)\n", flash, flash / (1024 * 1024));
    Serial.printf("psram            %u bytes\n", (unsigned)ESP.getPsramSize());
    Serial.printf("heap             %u bytes free of %u\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getHeapSize());
    Serial.printf("cpu              %u MHz\n", (unsigned)getCpuFrequencyMhz());
    Serial.printf("mac (wifi sta)   %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5]);
    Serial.printf("reset reason     %s\n", resetReasonName(esp_reset_reason()));
    Serial.printf("wake cause       %s\n", wakeCauseName(esp_sleep_get_wakeup_cause()));
}

// The RTC slow clock is what times deep sleep. A board with no 32.768 kHz
// crystal falls back to an internal RC oscillator that is temperature
// dependent, which is the difference between a clock that drifts seconds a day
// and one that drifts minutes.
static void reportRtcClock() {
    Serial.println("--- rtc clock ---");

    const uint32_t cal = esp_clk_slowclk_cal_get();
    // Calibration is the slow-clock period in microseconds, Q13.19 fixed point.
    const double periodUs = (double)cal / (double)(1 << 19);
    const double hz = periodUs > 0 ? 1000000.0 / periodUs : 0.0;

    const char* src = "unknown";
    switch ((int)rtc_clk_slow_freq_get()) {
        case RTC_SLOW_FREQ_RTC: src = "internal RC oscillator"; break;
        case RTC_SLOW_FREQ_32K_XTAL: src = "external 32.768 kHz crystal"; break;
        case RTC_SLOW_FREQ_8MD256: src = "internal 8 MHz / 256"; break;
    }
    Serial.printf("slow clock src   %s\n", src);
    Serial.printf("calibrated freq  %.1f Hz (nominal 32768 for a crystal)\n", hz);
    Serial.printf("rtc memory       boot #%u, canary %s\n", g_bootCount,
                  g_rtcCanary == kCanary ? "survived deep sleep" : "cold (first boot)");
}

// A pin with something driving it holds its level against both internal pulls.
// A floating pin follows whichever pull is enabled.
static void probePin(const char* name, int pin) {
    pinMode(pin, INPUT_PULLUP);
    delay(5);
    const int up = digitalRead(pin);
    pinMode(pin, INPUT_PULLDOWN);
    delay(5);
    const int down = digitalRead(pin);
    pinMode(pin, INPUT);
    delay(5);
    const int hiz = digitalRead(pin);

    const char* verdict;
    if (up == down) {
        verdict = up ? "DRIVEN HIGH (beats the pulldown)" : "DRIVEN LOW (beats the pullup)";
    } else {
        verdict = "floating / high-impedance";
    }
    Serial.printf("GPIO%-2d %-14s pullup=%d pulldown=%d hi-z=%d  -> %s\n", pin, name, up, down,
                  hiz, verdict);
}

static uint32_t readAdcMv(int pin, int samples = 16) {
    uint32_t total = 0;
    for (int i = 0; i < samples; i++) {
        total += analogReadMilliVolts(pin);
        delay(2);
    }
    return total / samples;
}

static void reportBattery() {
    Serial.println("--- battery (GPIO0) ---");
    analogSetPinAttenuation(kPinBatteryAdc, ADC_11db);
    const uint32_t mv = readAdcMv(kPinBatteryAdc, 32);
    Serial.printf("adc at the pin   %u mV\n", mv);
    Serial.printf("x2 divider       %u mV at the cell\n", mv * 2);
    Serial.printf("raw counts       %d\n", analogRead(kPinBatteryAdc));
    if (mv < 300) {
        Serial.println("note             implausibly low -- GPIO0 may not be the divider");
    } else if (mv * 2 > 3000 && mv * 2 < 4400) {
        Serial.println("note             consistent with a single LiPo cell through a 2:1 divider");
    } else {
        Serial.println("note             outside single-cell LiPo range, check the divider ratio");
    }
}

static void reportUsbDetect() {
    Serial.println("--- usb / charge detect (GPIO20) ---");
    probePin("usb-detect", kPinUsbDetect);
    Serial.println("expect this to flip when the cable is plugged and unplugged");
}

// Unplugging the cable takes the serial port with it, but not the power -- the
// battery keeps the sketch running. So the transitions are buffered and dumped
// once the host is back.
static void watchUsbDetect(uint32_t seconds) {
    struct Event {
        uint32_t ms;
        int level;
        uint16_t batMv;
    };
    static Event events[64];
    int n = 0;

    pinMode(kPinUsbDetect, INPUT);
    int last = digitalRead(kPinUsbDetect);
    const uint32_t start = millis();
    events[n++] = {0, last, (uint16_t)readAdcMv(kPinBatteryAdc, 8)};

    Serial.printf("[watch] GPIO20 starts at %d. Unplug the cable, wait ~5 s, plug it back in.\n",
                  last);
    Serial.printf("[watch] watching for %u s; the summary prints when the port returns.\n",
                  seconds);
    Serial.flush();

    while (millis() - start < seconds * 1000u) {
        const int now = digitalRead(kPinUsbDetect);
        if (now != last && n < 64) {
            last = now;
            events[n++] = {millis() - start, now, (uint16_t)readAdcMv(kPinBatteryAdc, 8)};
        }
        delay(20);
    }

    Serial.println("--- gpio20 transitions ---");
    for (int i = 0; i < n; i++) {
        Serial.printf("t+%-6u ms  GPIO20=%d  battery %u mV at the pin, %u mV at the cell\n",
                      events[i].ms, events[i].level, events[i].batMv, events[i].batMv * 2u);
    }
    if (n < 2) {
        Serial.println("no transitions seen -- GPIO20 did not react to the cable");
    } else {
        Serial.printf("GPIO20 is %s when USB is attached\n",
                      events[0].level ? "HIGH" : "LOW");
    }
    Serial.printf("USB CDC (HWCDC::isPlugged) now reads %d\n", (int)HWCDC::isPlugged());
}

static void scanI2c() {
    Serial.println("--- i2c scan (SDA=GPIO20, SCL=GPIO0) ---");
    Serial.println("the X3 carries a DS3231 RTC (0x68), a BQ27220 gauge (0x55) and a");
    Serial.println("QMI8658 IMU (0x6B) on this bus; the X4 is documented as having none.");

    Wire.begin(kPinI2cSda, kPinI2cScl, 100000);
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            const char* known = "";
            if (addr == 0x68) known = "  <- DS3231 RTC address";
            if (addr == 0x55) known = "  <- BQ27220 fuel gauge address";
            if (addr == 0x6B || addr == 0x6A) known = "  <- QMI8658 IMU address";
            Serial.printf("device at 0x%02X%s\n", addr, known);
            found++;
        }
        delay(2);
    }
    Serial.printf("%d device(s) responded\n", found);
    Wire.end();

    // Hand GPIO0 back to the ADC.
    pinMode(kPinI2cScl, INPUT);
    analogSetPinAttenuation(kPinBatteryAdc, ADC_11db);
}

static void dumpAllPins() {
    Serial.println("--- every readable gpio ---");
    // 18/19 are the USB D-/D+ pair and 11 is the flash voltage selector; leave
    // them alone. 9 is the BOOT strap, safe to read.
    static const int pins[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 20, 21};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        probePin("", pins[i]);
    }
}

// ------------------------------------------------------------------ buttons ---

struct LadderEntry {
    const char* name;
    int raw;
};

// Levels published by the open-x4 sample firmware. They are quoted there as
// millivolts, but they only line up with this unit as raw 12-bit counts -- and
// they have to be: the C3's ADC saturates around 3100 mV, so a "3470 mV" rung
// is not a reading the chip can produce.
static const LadderEntry kLadder1[] = {
    {"BACK", 3470}, {"CONFIRM", 2655}, {"LEFT", 1470}, {"RIGHT", 3}};
static const LadderEntry kLadder2[] = {{"UP", 2205}, {"DOWN", 3}};

static const char* nearestLadder(const LadderEntry* table, size_t n, int raw, int* offBy) {
    const char* best = "?";
    int bestDelta = 100000;
    for (size_t i = 0; i < n; i++) {
        const int d = abs(raw - table[i].raw);
        if (d < bestDelta) {
            bestDelta = d;
            best = table[i].name;
        }
    }
    *offBy = bestDelta;
    return best;
}

static int readAdcRaw(int pin, int samples = 8) {
    uint32_t total = 0;
    for (int i = 0; i < samples; i++) {
        total += analogRead(pin);
        delayMicroseconds(200);
    }
    return (int)(total / samples);
}

// GPIO20 is polled from the main loop rather than a one-shot command: pulling
// the cable takes the serial port with it but not the power, so transitions are
// buffered and replayed once the host comes back.
static void pollUsbDetect() {
    struct Event {
        uint32_t ms;
        int level;
        uint16_t batMv;
    };
    static Event pending[16];
    static int nPending = 0;
    static int last = -1;
    static bool everPlugged = false;

    const int now = digitalRead(kPinUsbDetect);
    if (last == -1) {
        last = now;
        return;
    }
    if (now != last) {
        last = now;
        if (nPending < 16) {
            pending[nPending++] = {millis(), now, (uint16_t)readAdcMv(kPinBatteryAdc, 4)};
        }
    }

    const bool plugged = HWCDC::isPlugged();
    if (plugged && !everPlugged) everPlugged = true;
    if (!plugged || nPending == 0) return;

    Serial.println("--- gpio20 transitions while the cable moved ---");
    for (int i = 0; i < nPending; i++) {
        Serial.printf("t=%-8u ms  GPIO20=%d  battery %u mV at pin, %u mV at cell\n", pending[i].ms,
                      pending[i].level, pending[i].batMv, pending[i].batMv * 2u);
    }
    Serial.printf("GPIO20 now %d with the cable attached -> USB-attached level is %s\n", now,
                  now ? "HIGH" : "LOW");
    nPending = 0;
}

static int g_idle1 = 0;
static int g_idle2 = 0;

static void calibrateIdle() {
    g_idle1 = readAdcRaw(kPinButtonAdc1, 32);
    g_idle2 = readAdcRaw(kPinButtonAdc2, 32);
    Serial.println("--- button ladder idle ---");
    Serial.printf("GPIO1 idle       raw %d (%u mV)\n", g_idle1, readAdcMv(kPinButtonAdc1, 8));
    Serial.printf("GPIO2 idle       raw %d (%u mV)\n", g_idle2, readAdcMv(kPinButtonAdc2, 8));
    pinMode(kPinPowerButton, INPUT_PULLUP);
    delay(5);
    Serial.printf("GPIO3 idle       %d (power button, expected 1 with a pullup)\n",
                  digitalRead(kPinPowerButton));
}

// Detection works in raw counts, not millivolts: BACK sits about 500 counts
// below the idle rail, but both are past the ADC's saturation knee, so in
// millivolts they are only tens of mV apart and the press is invisible.
static void watchButtons() {
    static int lastReported = -1;  // 0 none, 1 gpio1, 2 gpio2, 3 power
    const int mv1 = readAdcRaw(kPinButtonAdc1, 4);
    const int mv2 = readAdcRaw(kPinButtonAdc2, 4);
    const int power = digitalRead(kPinPowerButton);

    const bool active1 = abs(mv1 - g_idle1) > 200;
    const bool active2 = abs(mv2 - g_idle2) > 200;
    const bool activeP = power == LOW;

    int now = 0;
    if (activeP) now = 3;
    else if (active1) now = 1;
    else if (active2) now = 2;

    if (now == lastReported) return;
    lastReported = now;

    int offBy = 0;
    switch (now) {
        case 0:
            Serial.println("[button] released");
            break;
        case 1: {
            const char* name =
                nearestLadder(kLadder1, sizeof(kLadder1) / sizeof(kLadder1[0]), mv1, &offBy);
            Serial.printf("[button] GPIO1 raw %4d (%4u mV) -> %s, %d counts off the published rung\n",
                          mv1, readAdcMv(kPinButtonAdc1, 4), name, offBy);
            break;
        }
        case 2: {
            const char* name =
                nearestLadder(kLadder2, sizeof(kLadder2) / sizeof(kLadder2[0]), mv2, &offBy);
            Serial.printf("[button] GPIO2 raw %4d (%4u mV) -> %s, %d counts off the published rung\n",
                          mv2, readAdcMv(kPinButtonAdc2, 4), name, offBy);
            break;
        }
        case 3:
            Serial.println("[button] GPIO3 POWER (digital, active low)");
            break;
    }
}

// -------------------------------------------------------------------- sleep ---

static void sleepOnTimer(uint32_t ms) {
    g_requestedSleepMs = ms;
    g_sleepStartedUs = esp_timer_get_time();
    g_rtcCanary = kCanary;
    Serial.printf("SLEEPSTART %u\n", ms);
    Serial.flush();
    delay(20);
    esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
    esp_deep_sleep_start();
}

// The board dies when USB is removed, which for a battery clock is the whole
// ballgame. The X3 pin map calls GPIO13 "Power/SD control", and on this X4 it
// probes as floating -- nothing drives it. If it gates the battery rail, then
// holding it HIGH across deep sleep is what keeps the board alive unplugged.
// gpio_hold_en latches the level through sleep, when the GPIO matrix is off.
static void sleepHoldingPin(int pin, int level, uint32_t ms) {
    g_requestedSleepMs = ms;
    g_rtcCanary = kCanary;

    pinMode(pin, OUTPUT);
    digitalWrite(pin, level);
    delay(50);
    gpio_hold_en((gpio_num_t)pin);
    gpio_deep_sleep_hold_en();

    Serial.printf("[hold] GPIO%d driven %s and latched for deep sleep\n", pin,
                  level ? "HIGH" : "LOW");
    Serial.printf("SLEEPSTART %u\n", ms);
    Serial.flush();
    delay(20);
    esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
    esp_deep_sleep_start();
}

static void sleepOnButton() {
    g_requestedSleepMs = 0;
    g_rtcCanary = kCanary;
    pinMode(kPinPowerButton, INPUT_PULLUP);
    Serial.println("[sleep] waking on GPIO3 low -- press the power button");
    Serial.flush();
    esp_deep_sleep_enable_gpio_wakeup(1ULL << kPinPowerButton, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}

static void reportSleepAccuracy() {
    if (g_requestedSleepMs == 0) return;
    Serial.println("--- previous timer sleep ---");
    Serial.printf("requested        %u ms\n", g_requestedSleepMs);
    Serial.printf("wake cause       %s\n", wakeCauseName(esp_sleep_get_wakeup_cause()));
    Serial.printf("rtc memory       %s\n",
                  g_rtcCanary == kCanary ? "SURVIVED deep sleep" : "LOST across deep sleep");
    Serial.printf("boot count       %u (increments only if RTC memory holds)\n", g_bootCount);
    // esp_timer keeps counting through deep sleep, but off the same RTC clock
    // that timed the sleep, so this is self-consistent rather than accurate.
    // The host's wall clock is the reference; see docs/hardware.md.
    Serial.printf("SLEEPMARK %u\n", g_requestedSleepMs);
    g_requestedSleepMs = 0;
}

// --------------------------------------------------------------------- main ---

void setup() {
    latchBattery();  // before anything else, or an unplugged board dies here
    Serial.begin(115200);
    delay(2000);  // give the USB CDC host time to reattach after a reset
    g_bootCount++;

    Serial.println();
    Serial.println("=========== xteink x4 hardware probe ===========");
    historyAppend();
    historyDump();
    reportChip();
    reportRtcClock();
    reportSleepAccuracy();
    reportBattery();
    reportUsbDetect();
    // The I2C scan is deliberately NOT run at boot. Those "bus" pins are the
    // battery divider (GPIO0) and USB/charge detect (GPIO20) on this board, and
    // driving them as an open-drain bus repeatedly wedged the probe mid-report.
    // It answered its question once -- no devices -- so it lives behind 'i'.
    Serial.println("--- documented pin map under test ---");
    probePin("bat-adc", kPinBatteryAdc);
    probePin("btn-adc-1", kPinButtonAdc1);
    probePin("btn-adc-2", kPinButtonAdc2);
    probePin("power-btn", kPinPowerButton);
    probePin("epd-dc", 4);
    probePin("epd-rst", 5);
    probePin("epd-busy", 6);
    probePin("sd-miso", 7);
    probePin("spi-sck", 8);
    probePin("spi-mosi", 10);
    probePin("sd-cs", 12);
    // GPIO13 is deliberately NOT probed: it is the battery latch MOSFET, and
    // driving it against internal pulls switches the board off on battery. An
    // earlier run held it high for sleep and still died after one cycle, which
    // is exactly what this survey does to it on the following boot.
    probePin("epd-cs", 21);

    calibrateIdle();

    // A genuine repeat cycle always arrives as a deep-sleep wake. Any other
    // reset -- a reflash, a power-on, a terminal attaching -- means someone is
    // at the keyboard, so cancel rather than making them race a 15 s window
    // they cannot see the start of.
    if (repeatEnabled() && esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        repeatSet(false);
        Serial.println("[repeat] cancelled: this boot was not a deep-sleep wake");
    }

    if (repeatEnabled()) {
        Serial.println("--- repeat sleep mode ---");
        Serial.println("send 'x' within 15 s to stop and stay awake");
        Serial.flush();
        // Only 'x' escapes, and the window is long: any other stray byte -- an
        // 'h' or an 'R' arriving late from a previous step -- used to cancel the
        // run by accident, which is worse than waiting.
        const uint32_t until = millis() + 15000;
        while ((int32_t)(until - millis()) > 0) {
            while (Serial.available()) {
                if (Serial.read() == 'x') {
                    repeatSet(false);
                    Serial.println("[repeat] stopped, staying awake");
                    Serial.println("=========== commands: b i s g p h H R ===========");
                    return;
                }
            }
            delay(50);
        }
        sleepHoldingPin(kPinBatteryLatch, HIGH, 30000);
    }

    Serial.println("=========== press buttons; b i s g p for commands ===========");
}

void loop() {
    if (Serial.available()) {
        const int c = Serial.read();
        switch (c) {
            case 'b': reportBattery(); calibrateIdle(); break;
            case 'i': scanI2c(); calibrateIdle(); break;
            case 's': sleepOnTimer(20000); break;
            case 'S': sleepOnTimer(300000); break;
            case 'g': sleepOnButton(); break;
            case 'p': dumpAllPins(); calibrateIdle(); break;
            case 'u': reportUsbDetect(); calibrateIdle(); break;
            case 'w': watchUsbDetect(60); calibrateIdle(); break;
            case 'D': decayTest(60); break;
            case 'd': decayDump(); break;
            case 'h': historyDump(); break;
            case 'H': historyClear(); break;
            case 'L': sleepHoldingPin(13, HIGH, 300000); break;
            case 'R':
                historyClear();
                repeatSet(true);
                sleepOnTimer(30000);
                break;
            case 'K': sleepHoldingPin(13, LOW, 300000); break;
            default: break;
        }
    }
    watchButtons();
    pollUsbDetect();
    delay(20);
}
