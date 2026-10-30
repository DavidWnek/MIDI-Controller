// Debounced footswitch scanner with press / release / long-press events.
#pragma once
#include <Arduino.h>

enum SwitchEvent : uint8_t { SW_PRESS, SW_RELEASE, SW_LONG };
typedef void (*SwitchHandler)(uint8_t index, SwitchEvent ev);

void switchesBegin(SwitchHandler handler);
void switchesScan();                 // call every ~1 ms
bool switchIsDown(uint8_t index);
