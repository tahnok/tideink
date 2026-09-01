# Xteink X4 hardware notes

Everything below was measured on the actual unit this repository drives, with
the probe firmware in [`src/diag/main.cpp`](../src/diag/main.cpp):

```sh
pio run -e diag -t upload && pio device monitor
```

Published pin maps for this board disagree with each other in a few places, and
one widely copied table is wrong about its units. Where a source and the bench
disagree, the bench wins and the disagreement is called out.

Unit under test: ESP32-C3 revision 4, 16 MB flash, no PSRAM, MAC
`14:63:93:F5:1A:74`, 320 KB heap, 160 MHz.

## Pin map

Confirmed by driving each pin against both internal pulls and watching whether
the external circuit won. "Driven" means the board holds the level against a
~45 kΩ internal pull; "floating" means the pin follows whichever pull is on.

| GPIO | Function | Observed |
|---|---|---|
| 0 | Battery sense, 2:1 divider | ~2.05 V, i.e. half the cell |
| 1 | Button ladder A — back, confirm, left, right | driven high at idle |
| 2 | Button ladder B — up, down | driven high at idle |
| 3 | Power button, digital, active low | driven high at idle |
| 4 | E-paper DC | floating when idle |
| 5 | E-paper RST | floating when idle |
| 6 | E-paper BUSY | driven high |
| 7 | SPI MISO (microSD) | driven high |
| 8 | SPI SCK (shared panel/SD) | driven high |
| 10 | SPI MOSI (shared panel/SD) | driven high |
| 12 | microSD CS | driven high |
| 13 | **Battery latch MOSFET** — must be held HIGH to run unplugged | floating unless firmware drives it |
| 18/19 | USB D−/D+ | not probed |
| 20 | USB present / charge detect | driven high with a cable in, low without |
| 21 | E-paper CS | floating when idle |

This matches the pin map in
[`src/device/config.h`](../src/device/config.h) exactly.

## Buttons

The X4 has **seven** buttons, not one. Only the power button has its own GPIO;
the other six sit on two resistor ladders read by the ADC. The tide clock uses
only the power button, so the other six are free.

Each button was pressed in isolation and the ADC sampled at the press:

| Button | Pin | Measured (raw) | Published rung | Δ |
|---|---|---|---|---|
| BACK | GPIO1 | 3516 | 3470 | 46 |
| CONFIRM | GPIO1 | 2684 | 2655 | 29 |
| LEFT | GPIO1 | 1485 | 1470 | 15 |
| RIGHT | GPIO1 | 2 | 3 | 1 |
| UP | GPIO2 | 2222 | 2205 | 17 |
| DOWN | GPIO2 | 2 | 3 | 1 |
| POWER | GPIO3 | digital low | — | — |

Idle on both ladders is a saturated **4095**.

**The published levels are raw 12-bit ADC counts, not millivolts.** The
open-x4 sample firmware quotes them as millivolts, and they cannot be: the
C3's ADC saturates near 3100 mV at 11 dB, so a "3470 mV" rung is not a reading
the chip is capable of producing. Read as counts they match this unit to within
1.3%. Anything decoding these buttons must compare `analogRead()`, not
`analogReadMilliVolts()` — in millivolts, BACK and idle differ by only tens of
mV and the press is invisible.

## Battery

- Single LiPo cell on a **2:1 divider** into GPIO0 (2×10 kΩ per the open-x4
  sample firmware).
- Measured 2.05–2.11 V at the pin across the session, so **4.10–4.21 V at the
  cell**, rising slowly while plugged in — a charging cell.
- `BATTERY_DIVIDER 2.0` in `config.h` is **correct**.
- Capacity is published as 650 mAh. Not verified here; that needs a discharge
  test, not a probe.
- The percentage readout uses the FreeInk SDK's rest-voltage notch table
  (`libs/hardware/BatteryMonitor`, 3450 mV at 0% through 4200 mV at 100%),
  copied into `kLipoCurveMv` in `src/device/power.cpp`. Its 0% anchor is also
  what `BATTERY_CRITICAL_MV` is set to. Both are CrossPoint's numbers for this
  board rather than measurements of this unit; the 4.10–4.21 V seen above while
  charging is consistent with the 4200 mV top of that table, which is as much
  corroboration as a probe session can give.

There is **no fuel gauge**. The X3's BQ27220 is absent (see the I²C scan below),
so the percentage can only ever come from the voltage curve.

### The divider is probably what drains the battery

Not measured here, but worth stating plainly, because it changes what firmware is
even worth optimising. The resistor values are published rather than probed --
the [open-x4 sample firmware](https://github.com/open-x4-epaper/sample-firmware)
says "GPIO0 is connected to the battery via a voltage divider (2x10K resistors),
reading 1/2 of the actual voltage", and
[Adafruit's X4 pinout](https://learn.adafruit.com/circuitpython-on-the-xteink-x4-ereader/pinouts)
independently confirms a divider on GPIO0 without giving values. Nobody appears to
have published a measured sleep current for this board at all.

If 2×10 kΩ is right, that is 20 kΩ across the cell:

| | continuous | per day | from 650 mAh |
|---|---|---|---|
| Divider at 2×10 kΩ, cell at 3.9 V | ~195 µA | ~4.7 mAh | ~140 days |
| ESP32-C3 in deep sleep, RTC memory retained (datasheet) | ~5 µA | ~0.12 mAh | — |
| One wake a day: boot, download, full refresh | — | ~0.3 mAh | — |

That is arithmetic from a published resistor value and a datasheet figure, not a
bench reading, so treat the first row as a hypothesis. But if it holds, the
divider costs more than an order of magnitude more than everything the firmware
does put together, and no amount of scheduling will move the runtime much. 20 kΩ
is also aggressive for a battery divider -- hobby boards that care about sleep
current usually run 2×100 kΩ or higher -- which is itself a hint about where it
sits in the circuit.

**Which side of the GPIO13 latch is it on?** Unpublished, and it decides both how
bad this is and what the fix looks like. Downstream of the latch MOSFET, the
divider only draws while the board is powered, so the stock reader escapes it
entirely by dropping GPIO13 when the user closes it -- and a tide clock, which
holds that latch on for ever precisely so it *does* keep running, pays it around
the clock by design. Upstream, on the cell itself, it drains a shelf-stored X4
too. Either way the clock pays; only the stock firmware's behaviour differs.

**Measure it before optimising anything else.** Put a meter in series with the
cell and let the clock go to sleep; the reading settles a few seconds after the
panel finishes. Roughly 200 µA means the divider dominates and the fix is a
hardware one -- larger resistors, or a MOSFET gating the bottom leg from a spare
GPIO so the divider only draws while a reading is being taken. Roughly 5–10 µA
means the divider is already high-impedance and the firmware schedule is what
sets the runtime.

For a sense of what the second case looks like, the Inkplate 2 -- an ESP32 e-paper
board built for exactly this kind of once-a-day sketch -- is
[specified at 20–30 µA in deep sleep](https://docs.soldered.com/inkplate/2/low-power/deep-sleep/)
and has **no battery monitoring hardware at all**: `readBattery()` is
[unsupported on that model](https://inkplate.readthedocs.io/en/latest/arduino.html).
No divider, no leak, and no battery percentage on screen either. That is the
trade this board made in the other direction.

### The microSD slot is a power question, not a storage one

There is a microSD slot on the shared SPI bus (CS on GPIO12, MISO on GPIO7,
SCK/MOSI shared with the panel), and writing a month of predictions to it instead
of three days into RTC memory is an obvious-looking way to cut the radio down.
The arithmetic goes the wrong way, and not by a little.

An inserted card is powered whether or not anything ever talks to it. The socket
has a dedicated **VDD contact** wired to the 3.3 V rail -- the card is not sipping
parasitically off the SPI lines, it is a live device sitting in a live socket --
and it draws its idle current from the moment the rail comes up.

How much is entirely down to which card. Measurements from people who chase this
seriously:

| Card | idle in SPI mode | per day | 650 mAh lasts |
|---|---|---|---|
| Sandisk Ultra ([Gough Lui, 2021](https://goughlui.com/2021/02/27/experiment-microsd-card-power-consumption-spi-performance/)) | ~1.25 mA | ~30 mAh | ~3 weeks |
| Verbatim Premium (same) | ~1.45 mA | ~35 mAh | ~19 days |
| Genuine Sandisk in a logger ([Cave Pearl, 2014](https://thecavepearlproject.org/2014/09/22/high-sleep-current-problem-solved/)) | 0.2–0.3 mA | 5–7 mAh | 3–4 months |
| Best sleeper found ([Cave Pearl, 2017](https://thecavepearlproject.org/2017/05/21/switching-off-sd-cards-for-low-power-data-logging/)) | ~70 µA | ~1.7 mAh | ~1 year |
| Delkin industrial SLC (Gough Lui) | ~0.15 mA | ~3.6 mAh | ~6 months |
| Counterfeit cards, or pins left floating (Cave Pearl) | 2–5 mA | 48–120 mAh | days |

Against that, the most a card could save is the two HTTPS fetches -- around
0.15 mAh a day. The clock has to reach NTP daily regardless, because the RC
oscillator drifts about 1% and nothing on the card fixes that, so the Wi-Fi
association stays either way. The entire daily wake, boot and radio and full
panel refresh together, is only about 0.3 mAh.

So a typical consumer card costs **a hundred times** what it would save, and would
by itself flatten the battery in about three weeks. Even the best-behaved card
found in years of looking costs ten times the saving. The Cave Pearl loggers hit
exactly this -- sleeping cards ended up "the largest remaining power consumer" --
and the answer was to cut power to the card entirely between samples rather than
to manage its sleep current.

Which is the thing this board may not let us do. Nobody has published whether the
X4's slot has its own power gate, and the one candidate is GPIO13: the X3 pin map
calls it **"Power/SD control"**, and on the X4 it is the battery latch this
firmware has to hold HIGH to run at all. If that is one net, the card cannot be
powered down without powering the clock down with it.

And gating VDD is not on its own enough. Cut the card's power while the SPI lines
are still driven and it back-feeds through the protection diodes on CLK/CMD/DAT
and never actually sleeps -- the Cave Pearl write-up pulls MOSI, MISO and CS up
and CLK down, simultaneously, before dropping the rail. The related trap is
floating pins on a *powered* card: Sandisk's own note says the host has to pull
the unused lines up or "non-expected high current consumption may occur", which is
where those 2–5 mA readings come from. This board does put external pull-ups on
GPIO7 and GPIO12 (both probe as driven high), so it is at least not the worst
case.

So: **if there is a card in the slot, taking it out is almost certainly worth more
than any firmware change in this repository** -- plausibly more than the battery
divider too. The same meter test settles it: measure the sleeping current with a
card in, then with the slot empty. Only if the difference is negligible, or a
separate gate turns up, is SD worth revisiting; and then it buys robustness across
a battery swap, which RTC memory genuinely cannot survive, rather than runtime.

## Charge / USB detect — `config.h` was wrong

GPIO20 is **driven HIGH when USB is attached** and reads **LOW when the cable is
out**. Confirmed in both directions: the level beats an internal pulldown with a
cable in, and a wake logged while unplugged recorded it low.

`config.h` had `CHARGE_ACTIVE_LEVEL LOW`, which the README already flagged as a
guess. It was inverted, so `batteryCharging()` reported charging exactly when
the clock was not. Now set to `HIGH`.

GPIO20 is also UART0_RXD, and it is outside the GPIO 0–5 range the C3 can wake
from deep sleep on — which is why plugging in cannot wake a sleeping clock, as
the README describes.

## Clocks and RTC

**There is no external RTC.** An I²C scan on the X3's bus pins (SDA=GPIO20,
SCL=GPIO0) returned **zero devices**. All three X3 chips are absent:

| Chip | Address | X3 | X4 |
|---|---|---|---|
| DS3231 real-time clock | 0x68 | yes | **no** |
| BQ27220 fuel gauge | 0x55 | yes | **no** |
| QMI8658 IMU | 0x6B | yes | **no** |

Timekeeping is therefore entirely the ESP32-C3's internal RTC, disciplined by
NTP at each download.

**There is no 32.768 kHz crystal either.** The RTC slow clock runs from the
internal RC oscillator, calibrated at **147.3–147.8 kHz** across the session
(a crystal would read 32768). Consequences:

- Deep-sleep timing is only as good as an RC oscillator: measured within about
  **1%** of the requested duration at room temperature (a 20 s sleep landed at
  ~19.8 s, a 300 s sleep at ~298 s, both after subtracting ~2.25 s of boot and
  USB re-enumeration overhead).
- That error is temperature dependent, so it will be worse on a windowsill than
  on a bench.
- This is harmless for the tide clock only because it re-syncs from NTP every
  `DATA_REFRESH_HOURS`. Drift is bounded by one refresh interval — roughly a
  couple of minutes a day at worst, never accumulating.

RTC memory (`RTC_DATA_ATTR`) **does** survive deep sleep — verified by a canary
and a boot counter across timer wakes. It does *not* survive any other reset:
`.rtc.data` is re-initialised on every boot that is not a deep-sleep wake, so a
cold canary proves nothing about whether power was lost.

## GPIO13 is the battery latch — the board will not run unplugged without it

**This is the single most important thing on this page.** GPIO13 gates the
cell's path to the system rail through a MOSFET. Firmware that leaves it alone
gets a clock that runs only while USB supplies power: pull the cable and the
board dies within about a second, *while the divider still reads a full
4.15 V*. The voltage is there; the MOSFET is not passing it.

Nothing about that failure looks like a power problem from software. The
battery reads full, charging is detected, the panel keeps its image, and the
clock simply stops updating.

CrossPoint's `lib/hal/HalPowerManager.cpp` names the pin, and drives it **low
on purpose** — for a reader, "asleep on battery" means "off":

```cpp
constexpr gpio_num_t GPIO_SPIWP = GPIO_NUM_13;
gpio_set_direction(GPIO_SPIWP, GPIO_MODE_OUTPUT);
gpio_set_level(GPIO_SPIWP, 0);
gpio_hold_en(GPIO_SPIWP);
```
> "X4 GPIO13 is connected to the battery latch MOSFET. Keeping it low powers the
> MCU off on battery, while the SDK wake source still handles USB power."

A clock wants the opposite: **drive GPIO13 HIGH and keep it there**, including
through deep sleep. The GPIO matrix powers down while sleeping, so the level has
to be pinned with `gpio_hold_en()` or the pad floats and the board switches
itself off mid-sleep. See `latchBattery()` and `deepSleepFor()` in
[`src/device/power.cpp`](../src/device/power.cpp).

Ordering matters when re-asserting it on wake. While a pad is held, config
writes are staged but do not reach it, so drive the level *first* and call
`gpio_hold_dis()` after — releasing first lets the pad float for the few
microseconds before it is reconfigured, which is enough to drop the rail the
code is running from.

### Verified

With the latch asserted, a probe waking every ~45 s across a cable pull:

```
#1  timer  usb=present  cell=4180 mV
#2  timer  usb=ABSENT   cell=4178 mV  <- ran on battery
#3  timer  usb=ABSENT   cell=4178 mV  <- ran on battery
#4  timer  usb=ABSENT   cell=4176 mV  <- ran on battery
#5  timer  usb=ABSENT   cell=4176 mV  <- ran on battery
TALLY timer=5 poweron=0
```

Four consecutive wakes with no USB, a cell holding steady, and **zero power-on
events** — it was never reset, not even when the cable came back. Without the
latch the same test records no unplugged wake at all and a power-on at replug.

CrossPoint v1.5.0 carries a fix for "newer x4 battery latch issues", described
as newer X4s seeming "unresponsive unless connected to USB power". Units appear
to differ here, so a board that works without asserting GPIO13 is not evidence
that yours will.

### Running the latch backwards, on purpose

The tide clock now uses the *other* direction too. `powerOff()` in
[`src/device/power.cpp`](../src/device/power.cpp) drives GPIO13 low and pins it
there, which is exactly CrossPoint's power-off sequence, to switch the board off
when the cell reaches `BATTERY_CRITICAL_MV` — see
[Running out of battery](../README.md#running-out-of-battery).

Same ordering trap, mirrored: `gpio_hold_dis()` first so the pad will accept a
new level, then drive it low, then `gpio_hold_en()` again so the MOSFET gate is
held low rather than floating through whatever comes next.

**Not yet verified on hardware.** Two things about this are inference from the
observed "the board dies within about a second without the latch" behaviour
rather than measurements of their own:

- That releasing the latch while the board is *running* on battery cuts the rail
  as promptly as never asserting it does. The failure mode if it does not is
  benign — the code falls through to a deep sleep with the latch left released,
  which is where an unlatched board ends up anyway.
- How the board comes back afterwards. USB certainly works, since the cable
  supplies the rail regardless of the MOSFET. Whether the power button on GPIO3
  can also re-latch it depends on whether that button is wired into the gate as
  a hardware soft-latch or is only an MCU input, which the pin survey cannot
  distinguish. Assume USB until someone checks.

### Do not probe GPIO13

The pin survey in `src/diag/` deliberately skips it. Driving the battery latch
against internal pulls switches the board off on battery, and it will do so
one boot *after* the run that looked fine — which is exactly how this took
several passes to pin down.

### A measurement trap worth knowing

Two earlier conclusions in this investigation were wrong because of the
instrument, not the board:

1. **Attaching a serial terminal resets the chip.** pyserial asserts DTR/RTS on
   open, and on the C3's USB Serial/JTAG those lines drive CHIP_EN and IO0. Every
   "power-on reset on replug" was self-inflicted. Open the port with `dtr=False`
   and `rts=False` assigned *before* `open()` — and even then, expect a reset on
   attach here.
2. **RTC memory cannot detect power loss**, for the reason given above.

Both are why the final test writes to **NVS** and samples GPIO20 at wake: flash
survives resets and power loss alike, and the USB level at wake is direct
evidence rather than an inference from elapsed time.

## Reproducing

`pio run -e diag -t upload`, then single-key commands over serial:

| Key | Does |
|---|---|
| `b` | re-read the battery divider |
| `i` | re-scan I²C |
| `p` | probe every readable GPIO against both pulls |
| `s` | deep sleep 20 s on the timer |
| `g` | deep sleep until the power button is pressed |
| `h` / `H` | dump / clear the boot history in flash |
| `R` | repeat-sleep mode: wake every ~45 s, stamp USB state and cell mV to flash |
| `D` / `d` | arm the battery decay trace / read it back from flash |
| `x` | stop repeat mode during its 15 s escape window |

Repeat mode cancels itself on any boot that is not a deep-sleep wake, so
reflashing or attaching a terminal is always enough to get the prompt back.

The I²C scan runs only on `i`, never at boot: those "bus" pins are the battery
divider and the USB detect on this board, and driving them as an open-drain bus
repeatedly wedged the probe part-way through its report.

Pressing any button prints its ladder reading and the nearest published rung.

## Sources

- [Pinouts — CircuitPython on the Xteink X4 eReader, Adafruit](https://learn.adafruit.com/circuitpython-on-the-xteink-x4-ereader/pinouts)
- [open-x4-epaper/sample-firmware](https://github.com/open-x4-epaper/sample-firmware) — ladder levels and the 2×10 kΩ divider
- [Adafruit CircuitPython Xteink X4 library API](https://docs.circuitpython.org/projects/xteink_x4/en/latest/api.html)
- [bigbag/papyrix-reader device specifications](https://github.com/bigbag/papyrix-reader/blob/main/docs/device-specifications.md) — X3 vs X4 differences
- [Xteink X3 GPIO, from firmware analysis](https://gist.github.com/CrazyCoder/1c5f846adee18e21f91e264601a6ddce) — the X3 I²C peripherals this board lacks
- [crosspoint-reader `lib/hal/HalPowerManager.cpp`](https://github.com/crosspoint-reader/crosspoint-reader) — the GPIO13 battery latch, and the only source that names it
- [Free-Ink/freeink-sdk `libs/hardware/BatteryMonitor`](https://github.com/Free-Ink/freeink-sdk) — the discharge curve. CrossPoint's hardware code lives in this submodule, not in the reader repo, so grep here rather than there for anything battery, display or input related
- [usetrmnl/trmnl-firmware issue #313](https://github.com/usetrmnl/trmnl-firmware/issues/313) — X4 battery drain, community notes on its sleep behaviour

Note that the papyrix table lists GPIO20/GPIO0 as an I²C bus. That is the X3
arrangement; on the X4 those same pins are the charge detect and the battery
divider, and probing them as I²C finds nothing.
