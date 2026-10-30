// Configuration model (schema v2) and its JSON / LittleFS persistence.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "pins.h"

enum SwitchMode : uint8_t { MODE_MOMENTARY = 0, MODE_LATCH = 1, MODE_PAGE = 2, MODE_RADIO = 3, MODE_BANK = 4 };
enum MsgType    : uint8_t { TYPE_CC = 0, TYPE_NOTE = 1, TYPE_PC = 2 };
enum ActionType : uint8_t { ACT_CC = 0, ACT_NOTE_ON = 1, ACT_NOTE_OFF = 2, ACT_PC = 3, ACT_PAGE = 4, ACT_PRESET = 5 };

#define PAGE_PREV (-1)
#define PAGE_NEXT (-2)

struct MidiAction {
  ActionType t;
  uint8_t    ch;   // 0 = global channel
  int8_t     n;    // CC / note / program / page target / preset slot
  uint8_t    v;    // value / velocity
};

struct ActionList {
  uint8_t    count;
  MidiAction a[MAX_ACTIONS];
};

struct SwitchConfig {
  char       name[17];
  SwitchMode mode;
  MsgType    type;        // primary action (momentary / latch / radio)
  uint8_t    channel;     // 1..16, 0 = global
  uint8_t    number;
  uint8_t    onValue;
  uint8_t    offValue;
  int8_t     pageTarget;  // page mode
  uint32_t   color;       // 0xRRGGBB
  uint8_t    route;       // PORT_* bitmask for everything this switch sends
  uint8_t    group;       // radio group 1..8 (0 = none)
  int8_t     bankStep;    // bank mode: +/- program change step
  uint8_t    bankMin;
  uint8_t    bankMax;
  bool       bankWrap;
  ActionList extra;       // macro: sent on press after the primary action
  ActionList hold;        // long press
  ActionList tap2;        // double tap (replaces the press action for that tap)
};

struct PageConfig {
  char         name[17];
  SwitchConfig sw[NUM_SWITCHES];
};

struct DeviceConfig {
  char       name[25];
  uint8_t    midiChannel;   // 1..16
  uint8_t    startPage;
  uint8_t    brightness;    // 0..255
  uint8_t    pageCount;     // 1..MAX_PAGES
  uint8_t    thru[3];       // indexed by source PORT index (0 usb, 1 din, 2 net): bitmask of outputs
  uint32_t   blePin;        // 6-digit passkey, 0 = no pairing required
  PageConfig pages[MAX_PAGES];
};

extern DeviceConfig g_config;

void configSetDefaults(DeviceConfig &c);
void configToJson(const DeviceConfig &c, JsonObject out);
bool configFromJson(JsonObjectConst in, DeviceConfig &c, String &err);

bool configBegin();   // mounts LittleFS
bool configLoad();    // loads /config.json, falls back to defaults
bool configSave();    // writes /config.json
bool configFactoryReset();

const char *switchModeName(SwitchMode m);
const char *msgTypeName(MsgType t);
const char *actionTypeName(ActionType t);
uint8_t     configPortIndex(uint8_t portBit);   // PORT_USB -> 0, PORT_DIN -> 1, PORT_NET -> 2
