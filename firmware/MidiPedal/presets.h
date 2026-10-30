// On-device preset slots: whole configurations stored in LittleFS (/presets/N.json).
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void presetsList(JsonArray out);                       // [{slot,name,size}]
bool presetsSave(int slot, const char *name, String &err);   // current config -> slot
bool presetsLoad(int slot, String &err);               // slot -> current config (applied + saved)
bool presetsDelete(int slot, String &err);
