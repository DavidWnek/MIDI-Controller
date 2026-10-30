# MidiPedal

3x3 programmable MIDI foot controller on the Waveshare ESP32-S3-ETH (PoE), with a
cross-platform configuration app built with Avalonia.

**Firmware 0.2.0 features**: USB MIDI + RTP-MIDI over Ethernet + DIN out; up to 8 pages;
momentary / latch / radio-group / bank-step / page switches; macros, long-press and
double-tap actions; per-switch output routing; MIDI in on all ports with a thru matrix and
latch/radio/bank state sync; eight on-device presets; BLE passkey pairing; firmware updates
from the app (BLE or USB) and ArduinoOTA on Ethernet.

```
docs/       DESIGN.md (architecture, pin map, roadmap), PROTOCOL.md (BLE/serial JSON protocol)
firmware/   Arduino-ESP32 sketch + build.ps1 / build.sh (arduino-cli)
app/        Avalonia .NET 10 solution: shared UI + Desktop, Android, iOS, Browser heads
hardware/   wiring notes, future PCB
```

## Firmware quick start

Requirements: `arduino-cli` (Arduino IDE 2.x bundles one), esp32 core 3.3.x and four libraries.
One-time setup on any OS:

```bash
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install NimBLE-Arduino AppleMIDI "MIDI Library" ArduinoJson
```

Windows:

```powershell
cd firmware
.\build.ps1 upload COM6     # compile and flash (pass the pedal's current port; the script retries on the bootloader port)
.\build.ps1 monitor COM6    # console
```

macOS / Linux (the pedal shows up as `/dev/cu.usbmodem*`; the first flash of a fresh board
uses the chip's own USB-JTAG port, also `/dev/cu.usbmodem*`):

```bash
cd firmware
./build.sh upload /dev/cu.usbmodem1101
./build.sh monitor /dev/cu.usbmodem1101
```

Bench checks: `firmware/tools/bench-test.ps1` (Windows, drives the whole switch engine over
USB and captures the MIDI) and `firmware/tools/rtpmidi-probe.py` (any OS, joins the
RTP-MIDI session over Ethernet and prints what the pedal sends).

The board first appears as the ESP32-S3's built-in JTAG/serial port (COM4 here). After the first flash it enumerates as a composite USB device: a MIDI port and a
serial port both named after the device. On the network it advertises an RTP-MIDI
session (`_apple-midi._udp`) that macOS Audio MIDI Setup and rtpMIDI on Windows can join.

## App quick start

```powershell
cd app/MidiPedal
dotnet run --project MidiPedal.Desktop      # Windows/macOS/Linux, talks to the pedal over USB serial
dotnet build MidiPedal.Android              # needs: dotnet workload install android
```

Android and iOS connect over BLE (Plugin.BLE). iOS builds need a Mac with Xcode
(`dotnet workload install ios` there, then `dotnet build MidiPedal.iOS`).

On a Mac, the pedal's RTP-MIDI session appears in Audio MIDI Setup > MIDI Studio > Network
as "MidiPedal"; connect it and it becomes a normal CoreMIDI port.
