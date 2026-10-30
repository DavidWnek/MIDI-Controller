#include "status_led.h"
#include "pins.h"
#include "config_model.h"

static uint32_t s_idle = 0x000020;
static uint32_t s_flash = 0;
static uint32_t s_flashUntil = 0;
static uint32_t s_shown = 0xFFFFFFFF;

static void show(uint32_t rgb) {
  if (rgb == s_shown) return;
  s_shown = rgb;
  uint16_t b = g_config.brightness;
  uint8_t r = ((rgb >> 16) & 0xFF) * b / 255;
  uint8_t g = ((rgb >> 8) & 0xFF) * b / 255;
  uint8_t bl = (rgb & 0xFF) * b / 255;
  rgbLedWrite(PIN_STATUS_LED, r, g, bl);
}

void ledBegin() { show(0x0000FF); }

void ledSetIdle(uint32_t rgb) { s_idle = rgb; }

void ledFlash(uint32_t rgb, uint16_t ms) {
  s_flash = rgb;
  s_flashUntil = millis() + ms;
  s_shown = 0xFFFFFFFF;
}

void ledLoop() {
  if (s_flashUntil && (int32_t)(millis() - s_flashUntil) < 0) show(s_flash);
  else { s_flashUntil = 0; show(s_idle); }
}
