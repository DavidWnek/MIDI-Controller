// W5500 Ethernet + AppleMIDI (RTP-MIDI) session + mDNS + ArduinoOTA.
#pragma once
#include <Arduino.h>
#include "midi_router.h"

void   netMidiBegin(const char *deviceName, uint32_t otaPassword);
void   netMidiLoop();
void   netMidiSend(const MidiMsg &m);
bool   netLinkUp();
String netIp();
int    netSessions();
bool   netTakeLinkEvent(bool &up);  // true once per link change, for telemetry
