#pragma once
#include <Arduino.h>

void ledBegin();
void ledLoop();
void ledSetIdle(uint32_t rgb);                 // steady colour when nothing is happening
void ledFlash(uint32_t rgb, uint16_t ms);      // temporary colour (switch presses)
