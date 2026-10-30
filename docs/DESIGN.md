| USB-C | TinyUSB composite device: USB MIDI class + CDC serial (console and USB configuration). USB ID is 0x303A:0x4D50 for development; the Arduino default 0x303A:0x1001 collides with the chip's own JTAG/serial device and Windows then keeps its WinUSB driver bound to the MIDI interface. Get a real VID/PID (pid.codes or a vendor ID) before shipping |# MidiPedal — Design

A 3x3 programmable MIDI foot controller built on the Waveshare ESP32-S3-ETH (PoE),
configured from a phone/desktop app over Bluetooth LE or USB.

## 1. Hardware

| Item | Detail |
|---|---|
| MCU board | Waveshare ESP32-S3-ETH: ESP32-S3-WROOM-1 N16R8 (16 MB flash, 8 MB octal PSRAM), W5500 Ethernet, PoE module (802.3af), USB-C on native USB |
| Switches | 9 x SPST momentary, wired switch -> GPIO, other side -> GND. Internal pull-ups, active low |
| MIDI DIN/TRS out | UART1 TX at 31250 baud through the standard 2 x 220 ohm (5 V) / 33 + 10 ohm (3.3 V) resistor pair to a 5-pin DIN or TRS-A jack |
| MIDI DIN in (optional) | UART1 RX through a 6N138/H11L1 optocoupler |
| Status LED | On-board WS2812 on GPIO21 |
| Expansion | I2C on GPIO47/48 for a future OLED or port expander; ADC-capable pins kept free for an expression pedal |

### Pin map

GPIO 33-37 are the octal PSRAM bus, 19/20 are USB, 9-14 are the W5500, 0/3/45/46 are
strapping pins. Everything below is on the header.

| Function | GPIO |
|---|---|
| SW1 SW2 SW3 (top row, left to right) | 1, 2, 15 |
| SW4 SW5 SW6 | 16, 17, 18 |
| SW7 SW8 SW9 | 38, 39, 40 |
| MIDI OUT (UART1 TX) | 41 |
| MIDI IN (UART1 RX) | 42 |
| I2C SDA / SCL (expansion) | 48 / 47 |
| WS2812 status LED | 21 |
| W5500 MOSI / MISO / SCLK / CS / INT / RST | 11 / 12 / 13 / 14 / 10 / 9 |
| TF card (unused) | 4, 5, 6, 7 |
| Free | 0 (BOOT), 3, 43/44 (UART0), 45, 46 |

The pin table lives in one place in firmware (`firmware/MidiPedal/pins.h`).

## 2. Firmware

Arduino-ESP32 core 3.3.11 (built on ESP-IDF 5.5), compiled and flashed with `arduino-cli`.
Chosen over bare ESP-IDF because the core already ships native USB MIDI (TinyUSB),
W5500 Ethernet and mDNS, and the AppleMIDI (RTP-MIDI) and NimBLE libraries drop in.
If we ever need it, the same code can move to ESP-IDF with Arduino as a component.

### Transports (all active at once, every event is fanned out to all of them)

| Port | How |
|---|---|
| USB-C | TinyUSB composite device: USB MIDI class + CDC serial (console and USB configuration) |
| Ethernet | RTP-MIDI / AppleMIDI session on UDP 5004/5005, advertised with mDNS `_apple-midi._udp`. Shows up in macOS Audio MIDI Setup, Windows rtpMIDI, iOS Network MIDI |
| DIN / TRS | UART1, 31250 baud |
| (later) BLE MIDI | Second GATT service next to the config service |

### Switch engine

* 1 kHz scan, 8 ms debounce, press / release / long-press (800 ms) / double-tap (320 ms) events.
* Pages: up to 8, each holding 9 switch definitions. Active page is runtime state.
* Switch modes:
  * **momentary** — "on" message on press, "off" message on release.
  * **latch** — toggles on each press, holds state per page.
  * **radio** — latch with group exclusivity (amp channels, snapshots): selecting one
    deselects the others in the group and sends their off form.
  * **bank** — steps a per-channel program-change counter with min / max / step / wrap.
  * **page** — jumps to an absolute page, next or previous.
* Primary message types: CC (on/off value), Note (on/off), Program Change (press only).
* Per switch: a macro list sent after the primary action, a long-press list and a
  double-tap list (up to 6 messages each: CC, note on/off, PC, page jump, preset load),
  and an output route (any mix of USB, DIN, network).
* Per-switch MIDI channel, 0 = use the device's global channel.
* MIDI in on all three ports is parsed; a thru matrix forwards each input to any outputs,
  and incoming CC / PC keeps latch, radio and bank state (and the app's display) in sync
  with the rig.

### Configuration storage

Config is a single JSON document kept in LittleFS at `/config.json`; eight preset slots
live at `/presets/N.json`. The same JSON is what the app sends and receives, so there is
one schema everywhere (documented in `docs/PROTOCOL.md`). v1 documents still load.

### Updates and security

* Firmware updates stream over the control protocol (BLE or USB) as base64 chunks into
  the inactive OTA slot, MD5-checked; the pedal reboots into the new image. ArduinoOTA on
  Ethernet is also enabled for bench use.
* With a 6-digit passkey configured, the BLE service requires an authenticated bond
  before any read or write (LE Secure Connections, MITM). Bonds can be wiped from the app.

### Control protocol (BLE and USB serial, identical)

Newline-delimited JSON. One request -> one response, plus unsolicited events.

Requests: `hello`, `info`, `get`, `set` (full config, applied live and saved), `page`,
`press`/`release` (simulate a switch), `midi` (send an arbitrary message), `reset`
(factory defaults), `reboot`.

Events: `hello`, `info`, `config`, `ok`, `error`, `switch` (live press state),
`page`, `latch`, `link` (Ethernet up/down, IP).

BLE: one service, `7a1c0001-...` with an RX characteristic (write) and a TX
characteristic (notify). Messages longer than the MTU are split across writes/notifies
and reassembled on `\n`. Protocol is documented in `docs/PROTOCOL.md`.

## 3. App (Avalonia, .NET 10)

`app/MidiPedal/` is the `avalonia.xplat` template: one shared UI project plus Desktop,
Android, iOS and Browser heads. Platform-specific transport lives behind
`IDeviceLinkProvider`:

| Head | Transport |
|---|---|
| Desktop (Windows/macOS/Linux) | USB serial (System.IO.Ports) — no BLE needed for bench work |
| Android, iOS | BLE via Plugin.BLE |
| Browser | none yet (Web Bluetooth / Web Serial later) |

Screens: device picker -> page tabs -> 3x3 grid -> switch editor, with live press/latch
indicators because the device streams `switch`/`latch` events.

## 4. Feature roadmap

What a programmable MIDI controller usually grows into, ordered by value for effort.

### Tier 1 — implemented (firmware 0.2.0)
* Long-press and double-tap secondary actions per switch.
* Macros: several MIDI messages per press (e.g. PC + two CCs for a full patch change).
* Radio groups: switches on a page that are mutually exclusive (amp channels, snapshots).
* Bank step: PC increment/decrement with min/max and wrap.
* Per-switch output routing: choose USB / DIN / Network per switch.
* MIDI-in state sync: incoming CC/PC updates latch states and the LEDs so the
  pedal never disagrees with the DAW or amp.
* MIDI thru/merge: DIN in and USB in merged onto the other outputs.
* Presets: save/load whole configurations, import/export JSON from the app.
* BLE pairing with a passkey so a stranger's phone cannot reconfigure a stage rig.
* OTA firmware update from the app (BLE) and from the network.

### Tier 2 — strong differentiators
* Expression pedal input (TRS jack, ADC, calibration, curve, CC assign).
* Tap tempo -> MIDI clock out, and MIDI clock in -> tempo LED.
* Per-switch RGB LEDs (WS2812 ring under each cap) and a small OLED showing page and switch names.
* Setlist/song mode: pages as songs, with ordered setlists.
* USB HID keyboard mode: switches as shortcuts (DAW transport, OBS scenes, teleprompter).
* OSC output over Ethernet for lighting and show control (QLab, Resolume, TouchDesigner).
* RTP-MIDI initiator mode (pedal connects to a named host) and static IP.
* Web configuration page served over Ethernet as a BLE fallback.

### Tier 3 — hardware revisions
* Relay outputs for amp switching.
* External footswitch and second expression jack.
* PoE detection and power-source status in the app.
* Daisy-chain or splitter for multiple pedals on one network.
