// MIDI I/O hub: fans outgoing messages out to the selected ports, parses incoming
// MIDI from USB / DIN / network, applies thru routing and hands inputs to the engine.
#pragma once
#include <Arduino.h>

struct MidiMsg {
  uint8_t status;  // status byte including channel (0xB0 | ch)
  uint8_t data1;
  uint8_t data2;
  uint8_t len;     // 2 or 3
};

MidiMsg midiCC(uint8_t channel1to16, uint8_t cc, uint8_t value);
MidiMsg midiNoteOn(uint8_t channel1to16, uint8_t note, uint8_t velocity);
MidiMsg midiNoteOff(uint8_t channel1to16, uint8_t note, uint8_t velocity);
MidiMsg midiProgramChange(uint8_t channel1to16, uint8_t program);

typedef void (*MidiInHandler)(uint8_t sourcePort, const MidiMsg &m);

void midiRouterBegin();
void midiRouterLoop();
void midiRouterSend(const MidiMsg &m, uint8_t ports);          // PORT_* bitmask
void midiRouterSetInputHandler(MidiInHandler h);
void midiRouterDispatchInput(uint8_t sourcePort, const MidiMsg &m);   // thru + handler
bool midiUsbMounted();
