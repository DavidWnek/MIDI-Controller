// The switch engine: maps debounced switch events onto MIDI actions, pages, radio
// groups and bank counters, and keeps latch state in sync with incoming MIDI.
#pragma once
#include <Arduino.h>
#include "switches.h"
#include "midi_router.h"

void    controllerBegin();
void    controllerLoop();                       // timers (double-tap / deferred press / presets)
void    controllerOnSwitch(uint8_t index, SwitchEvent ev);
void    controllerOnMidiIn(uint8_t sourcePort, const MidiMsg &m);
void    controllerSimulate(uint8_t index, bool down);
bool    controllerSetPage(int index);           // absolute, PAGE_PREV or PAGE_NEXT
uint8_t controllerPage();
bool    controllerLatched(uint8_t page, uint8_t index);
int     controllerBankValue(uint8_t channel1to16);
void    controllerConfigChanged();
