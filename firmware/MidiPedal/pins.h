// MidiPedal - board pin map and build-wide constants for the Waveshare ESP32-S3-ETH.
// Everything hardware-specific lives here. See docs/DESIGN.md for the reasoning.
#pragma once
#include <Arduino.h>
#include <USBCDC.h>

#define FW_VERSION       "0.2.0"
#define PROTOCOL_VERSION 2

// USB identity. 0x303A:0x1001 (the Arduino default) is also the chip's built-in JTAG/serial
// device, so Windows would keep its WinUSB binding and never load the MIDI class driver.
// 0x4D50 ("MP") is a development PID only: get a properly assigned VID/PID before shipping.
#define USB_DEV_VID 0x303A
#define USB_DEV_PID 0x4D50

#define NUM_SWITCHES 9
#define MAX_PAGES    8
#define MAX_ACTIONS  6      // per action list (macro / hold / double-tap)
#define MAX_PRESETS  8      // on-device preset slots

// 3x3 footswitch grid, row-major (top-left = SW1). Switch to GND, internal pull-up.
static const uint8_t SWITCH_PINS[NUM_SWITCHES] = {1, 2, 15, 16, 17, 18, 38, 39, 40};

// 5-pin DIN / TRS MIDI on UART1.
#define PIN_MIDI_TX 41
#define PIN_MIDI_RX 42

// On-board WS2812.
#define PIN_STATUS_LED 21

// Reserved for an OLED / port expander later.
#define PIN_I2C_SDA 48
#define PIN_I2C_SCL 47

// W5500 Ethernet controller (fixed by the board).
#define W5500_MOSI     11
#define W5500_MISO     12
#define W5500_SCLK     13
#define W5500_CS       14
#define W5500_INT      10
#define W5500_RST      9
#define W5500_PHY_ADDR 1
#define W5500_SPI_MHZ  20

// Timing
#define SWITCH_DEBOUNCE_MS   8
#define SWITCH_LONGPRESS_MS  800
#define SWITCH_DOUBLETAP_MS  320

// Output / input port bitmasks (routing and thru).
#define PORT_USB 0x01
#define PORT_DIN 0x02
#define PORT_NET 0x04
#define PORT_ALL 0x07

// Console: USB CDC (TinyUSB mode; the CDC interface is started in setup()).
extern USBCDC USBSerial;
#define Console USBSerial
// USBCDC::write() returns at once while no host holds DTR, so logging never blocks the loop.
#define LOG(...)  do { Console.print("# "); Console.printf(__VA_ARGS__); Console.print("\n"); } while (0)
