#include "switches.h"
#include "pins.h"

static SwitchHandler s_handler = nullptr;
static bool     s_raw[NUM_SWITCHES];
static bool     s_stable[NUM_SWITCHES];
static uint32_t s_lastChange[NUM_SWITCHES];
static uint32_t s_downSince[NUM_SWITCHES];
static bool     s_longFired[NUM_SWITCHES];

void switchesBegin(SwitchHandler handler) {
  s_handler = handler;
  for (int i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);
    s_raw[i] = s_stable[i] = false;
    s_lastChange[i] = s_downSince[i] = 0;
    s_longFired[i] = false;
  }
}

bool switchIsDown(uint8_t i) { return i < NUM_SWITCHES && s_stable[i]; }

void switchesScan() {
  uint32_t now = millis();
  for (int i = 0; i < NUM_SWITCHES; i++) {
    bool down = digitalRead(SWITCH_PINS[i]) == LOW;
    if (down != s_raw[i]) {
      s_raw[i] = down;
      s_lastChange[i] = now;
    }
    if (down != s_stable[i] && (now - s_lastChange[i]) >= SWITCH_DEBOUNCE_MS) {
      s_stable[i] = down;
      if (down) {
        s_downSince[i] = now;
        s_longFired[i] = false;
        if (s_handler) s_handler(i, SW_PRESS);
      } else {
        if (s_handler) s_handler(i, SW_RELEASE);
      }
    }
    if (s_stable[i] && !s_longFired[i] && (now - s_downSince[i]) >= SWITCH_LONGPRESS_MS) {
      s_longFired[i] = true;
      if (s_handler) s_handler(i, SW_LONG);
    }
  }
}
