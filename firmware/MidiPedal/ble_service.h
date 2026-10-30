// BLE GATT configuration service (NimBLE). One RX (write) and one TX (notify)
// characteristic carrying newline-delimited JSON, chunked to the negotiated MTU.
// With a non-zero passkey the characteristics require an authenticated, bonded link.
#pragma once
#include <Arduino.h>

#define BLE_SERVICE_UUID "7a1c0001-5e4d-4c1b-9a2f-0d6e8b3f1a10"
#define BLE_RX_UUID      "7a1c0002-5e4d-4c1b-9a2f-0d6e8b3f1a10"
#define BLE_TX_UUID      "7a1c0003-5e4d-4c1b-9a2f-0d6e8b3f1a10"

void bleBegin(const char *deviceName, uint32_t passkey);
void bleLoop();
bool bleConnected();
int  bleBondCount();
void bleForgetBonds();
