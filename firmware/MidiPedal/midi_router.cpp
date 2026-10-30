#include "midi_router.h"
#include "pins.h"
#include "config_model.h"
#include "net_midi.h"
#include <USB.h>
#include <USBMIDI.h>
#include <driver/gpio.h>

extern "C" bool tud_mounted(void);

// Declared globally so its constructor registers the MIDI interface with TinyUSB
// before USB.begin() runs.
USBMIDI usbMidi("MidiPedal");

static HardwareSerial &dinSerial = Serial1;
static MidiInHandler   s_inHandler = nullptr;

static MidiMsg make(uint8_t status, uint8_t ch, uint8_t d1, uint8_t d2, uint8_t len) {
  if (ch < 1) ch = 1;
  if (ch > 16) ch = 16;
  MidiMsg m;
  m.status = status | (ch - 1);
  m.data1 = d1 & 0x7F;
  m.data2 = d2 & 0x7F;
  m.len = len;
  return m;
}
MidiMsg midiCC(uint8_t ch, uint8_t cc, uint8_t v)        { return make(0xB0, ch, cc, v, 3); }
MidiMsg midiNoteOn(uint8_t ch, uint8_t n, uint8_t vel)   { return make(0x90, ch, n, vel, 3); }
MidiMsg midiNoteOff(uint8_t ch, uint8_t n, uint8_t vel)  { return make(0x80, ch, n, vel, 3); }
MidiMsg midiProgramChange(uint8_t ch, uint8_t program)   { return make(0xC0, ch, program, 0, 2); }

void midiRouterBegin() {
  usbMidi.begin();
  dinSerial.setRxBufferSize(512);
  dinSerial.begin(31250, SERIAL_8N1, PIN_MIDI_RX, PIN_MIDI_TX);
  gpio_pullup_en((gpio_num_t)PIN_MIDI_RX);   // idle-high when no MIDI IN circuit is attached
}

bool midiUsbMounted() { return tud_mounted(); }

void midiRouterSetInputHandler(MidiInHandler h) { s_inHandler = h; }

void midiRouterSend(const MidiMsg &m, uint8_t ports) {
  if ((ports & PORT_USB) && tud_mounted()) {
    // For channel voice messages the USB Code Index Number equals the status high nibble.
    midiEventPacket_t p;
    p.header = (m.status >> 4) & 0x0F;
    p.byte1 = m.status;
    p.byte2 = m.data1;
    p.byte3 = m.len > 2 ? m.data2 : 0;
    usbMidi.writePacket(&p);
  }
  if (ports & PORT_DIN) {
    dinSerial.write(m.status);
    dinSerial.write(m.data1);
    if (m.len > 2) dinSerial.write(m.data2);
  }
  if (ports & PORT_NET) netMidiSend(m);
}

void midiRouterDispatchInput(uint8_t sourcePort, const MidiMsg &m) {
  uint8_t thru = g_config.thru[configPortIndex(sourcePort)] & ~sourcePort;
  if (thru) midiRouterSend(m, thru);
  if (s_inHandler) s_inHandler(sourcePort, m);
}

// ---- DIN input: minimal channel-voice parser with running status --------------------

static uint8_t s_dinStatus = 0, s_dinNeed = 0, s_dinHave = 0;
static uint8_t s_dinData[2];

static void dinFeed(uint8_t b) {
  if (b >= 0xF8) return;                       // real-time: ignored (clock sync is a roadmap item)
  if (b & 0x80) {
    if (b >= 0xF0) { s_dinStatus = 0; return; } // system common / sysex: not tracked
    s_dinStatus = b;
    s_dinHave = 0;
    s_dinNeed = ((b & 0xE0) == 0xC0) ? 1 : 2;   // PC and channel pressure carry one data byte
    return;
  }
  if (!s_dinStatus) return;
  s_dinData[s_dinHave++] = b;
  if (s_dinHave >= s_dinNeed) {
    MidiMsg m;
    m.status = s_dinStatus;
    m.data1 = s_dinData[0];
    m.data2 = s_dinNeed > 1 ? s_dinData[1] : 0;
    m.len = s_dinNeed + 1;
    s_dinHave = 0;
    midiRouterDispatchInput(PORT_DIN, m);
  }
}

void midiRouterLoop() {
  midiEventPacket_t p;
  while (usbMidi.readPacket(&p)) {
    uint8_t cin = p.header & 0x0F;
    if (cin < 0x8 || cin > 0xE) continue;        // only channel voice messages
    MidiMsg m;
    m.status = p.byte1;
    m.data1 = p.byte2;
    m.data2 = p.byte3;
    m.len = (cin == 0xC || cin == 0xD) ? 2 : 3;
    midiRouterDispatchInput(PORT_USB, m);
  }
  int guard = 64;
  while (dinSerial.available() && guard--) dinFeed((uint8_t)dinSerial.read());
}
