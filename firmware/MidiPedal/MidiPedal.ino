// MidiPedal - 3x3 programmable MIDI foot controller on the Waveshare ESP32-S3-ETH.
//
// Outputs:  USB MIDI (TinyUSB), RTP-MIDI over Ethernet (AppleMIDI), DIN/TRS MIDI (UART1)
// Inputs:   the same three ports, merged/thru'd and used to keep latch state in sync
// Config:   BLE GATT service or the USB CDC serial port, same JSON protocol (docs/PROTOCOL.md)
// Updates:  over the protocol (app) or ArduinoOTA on Ethernet
//
// Build:    firmware/build.ps1   (arduino-cli, esp32 core 3.3.x, USB mode = TinyUSB)

#include <USB.h>
#include "pins.h"
#include "config_model.h"
#include "switches.h"
#include "controller.h"
#include "midi_router.h"
#include "net_midi.h"
#include "ble_service.h"
#include "status_led.h"
#include "protocol.h"

USBCDC USBSerial;

// DeviceConfig and the JSON documents are large; give setup()/loop() room to breathe.
SET_LOOP_TASK_STACK_SIZE(24 * 1024);

class SerialLink : public Link {
 public:
  void sendLine(const String &line) override {
    // Dropped by the CDC driver while no host holds DTR; blocks at most 250 ms otherwise.
    Console.print(line);
    Console.print('\n');
  }
  const char *name() const override { return "usb"; }
};
static SerialLink s_serialLink;
static String     s_serialBuf;

static void pollSerial() {
  int guard = 4096;
  while (Console.available() && guard--) {
    char c = (char)Console.read();
    if (c == '\n') {
      if (s_serialBuf.length() && s_serialBuf[0] == '{') protocolHandleLine(s_serialBuf, &s_serialLink);
      s_serialBuf = "";
    } else if (c != '\r') {
      s_serialBuf += c;
      if (s_serialBuf.length() > 20000) s_serialBuf = "";
    }
  }
}

void setup() {
  configBegin();
  configLoad();

  // USB: MIDI interface (registered by its global constructor) + CDC console.
  USB.VID(USB_DEV_VID);
  USB.PID(USB_DEV_PID);
  USB.manufacturerName("MidiPedal");
  USB.productName(g_config.name);
  USBSerial.setRxBufferSize(16384);     // firmware updates arrive as 4 KB lines
  USBSerial.begin(115200);
  midiRouterBegin();
  USB.begin();

  ledBegin();
  protocolBegin();
  protocolRegisterLink(&s_serialLink);
  switchesBegin(controllerOnSwitch);
  controllerBegin();
  bleBegin(g_config.name, g_config.blePin);
  netMidiBegin(g_config.name, g_config.blePin);

  LOG("MidiPedal fw %s ready", FW_VERSION);
}

void loop() {
  static uint32_t lastScanUs = 0;
  uint32_t nowUs = micros();
  if ((uint32_t)(nowUs - lastScanUs) >= 1000) {
    lastScanUs = nowUs;
    switchesScan();
  }
  controllerLoop();
  midiRouterLoop();
  netMidiLoop();
  bleLoop();
  pollSerial();
  protocolLoop();
  ledLoop();
  delayMicroseconds(200);
}
