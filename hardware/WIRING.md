# Wiring notes

## Footswitches (9 x SPST momentary)

One side of every switch to GND, the other side to its GPIO. The firmware enables the
internal pull-up, so no external resistors are needed. Keep runs short or add 100 nF to
GND at the GPIO if the enclosure picks up noise.

| Switch | GPIO | Switch | GPIO | Switch | GPIO |
|---|---|---|---|---|---|
| SW1 (top-left) | 1 | SW2 | 2 | SW3 | 15 |
| SW4 | 16 | SW5 | 17 | SW6 | 18 |
| SW7 | 38 | SW8 | 39 | SW9 (bottom-right) | 40 |

## MIDI OUT (5-pin DIN or TRS type A), 3.3 V logic

Per the MIDI Association 3.3 V spec:

```
GPIO41 (TX) ──[ 10 Ω ]── DIN pin 5  (TRS tip)
3V3        ──[ 33 Ω ]── DIN pin 4  (TRS ring)
GND        ─────────── DIN pin 2  (TRS sleeve)   (shield; leave unconnected on an input)
```

If you would rather drive it from 5 V (VBUS/VSYS) through a 74HCT buffer, use 2 x 220 Ω.

## MIDI IN (optional)

```
DIN pin 4 ──[ 220 Ω ]──►|── 6N138 pin 2 (anode)        1N4148 across pins 2/3, reverse-biased
DIN pin 5 ───────────────── 6N138 pin 3 (cathode)
6N138 pin 8 ── 3V3,  pin 5 ── GND,  pin 6 ── GPIO42 (RX) with 1 kΩ pull-up to 3V3
```

## Power

* USB-C or the Waveshare PoE module (802.3af). Both can be connected at once.
* The W5500 and the ESP32-S3 radio together draw ~250 mA peaks; a PoE class 0 budget is plenty.

## Status LED

On-board WS2812 on GPIO21. Blue = booting / no Ethernet, green = Ethernet up, flashes
the switch colour on each press. A per-switch LED ring is on the roadmap (WS2812 chain
from a spare GPIO such as 47/48 or 43/44).

## Reserved / do not use

GPIO 33-37 (octal PSRAM), 19/20 (USB), 9-14 (W5500), 0/3/45/46 (strapping pins).
