// Firmware update over the control protocol (BLE or USB serial): the app streams the
// .bin as base64 chunks; the ESP32 Update library writes the inactive OTA slot.
#pragma once
#include <Arduino.h>

bool   otaBegin(size_t size, const char *md5, String &err);
bool   otaWriteBase64(const char *b64, size_t len, String &err);
bool   otaEnd(String &err);          // true: image valid, reboot to activate
void   otaAbort();
bool   otaActive();
size_t otaReceived();
size_t otaTotal();
