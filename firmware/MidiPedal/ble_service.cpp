#include "ble_service.h"
#include "pins.h"
#include "protocol.h"
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static NimBLEServer         *s_server = nullptr;
static NimBLECharacteristic *s_tx = nullptr;
static volatile bool         s_connected = false;
static volatile uint16_t     s_connHandle = 0;
static SemaphoreHandle_t     s_rxMutex;
static String                s_rxPending;     // bytes received, awaiting '\n'
static String                s_rxLines;       // complete lines waiting for the loop task
static bool                  s_secured = false;

class BleLink : public Link {
 public:
  void sendLine(const String &line) override {
    if (!s_connected || !s_tx) return;
    uint16_t mtu = s_server ? s_server->getPeerMTU(s_connHandle) : 23;
    size_t chunk = mtu > 3 ? mtu - 3 : 20;
    if (chunk > 244) chunk = 244;
    String payload = line + "\n";
    const uint8_t *p = (const uint8_t *)payload.c_str();
    size_t left = payload.length();
    while (left > 0 && s_connected) {
      size_t n = left < chunk ? left : chunk;
      s_tx->setValue(p, n);
      s_tx->notify();
      p += n;
      left -= n;
      if (left > 0) delay(3);   // keep the host's notification queue happy
    }
  }
  const char *name() const override { return "ble"; }
};
static BleLink s_link;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
    s_connHandle = info.getConnHandle();
    s_connected = true;
    LOG("ble connected");
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override {
    s_connected = false;
    LOG("ble disconnected (%d)", reason);
    xSemaphoreTake(s_rxMutex, portMAX_DELAY);
    s_rxPending = "";
    xSemaphoreGive(s_rxMutex);
  }
  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    if (s_secured && !info.isEncrypted()) {
      LOG("ble pairing failed, dropping link");
      NimBLEDevice::getServer()->disconnect(info.getConnHandle());
    } else {
      LOG("ble link %s", info.isBonded() ? "bonded" : "authenticated");
    }
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &) override {
    NimBLEAttValue v = chr->getValue();
    xSemaphoreTake(s_rxMutex, portMAX_DELAY);
    for (size_t i = 0; i < v.size(); i++) {
      char c = (char)v.data()[i];
      if (c == '\n') {
        if (s_rxPending.length()) { s_rxLines += s_rxPending; s_rxLines += '\n'; }
        s_rxPending = "";
      } else if (c != '\r') {
        s_rxPending += c;
        if (s_rxPending.length() > 20000) s_rxPending = "";   // runaway guard
      }
    }
    xSemaphoreGive(s_rxMutex);
  }
};

void bleBegin(const char *deviceName, uint32_t passkey) {
  s_rxMutex = xSemaphoreCreateMutex();
  s_secured = passkey != 0;

  NimBLEDevice::init(deviceName);
  NimBLEDevice::setMTU(517);
  NimBLEDevice::setPower(9);
  if (s_secured) {
    // Bonded + MITM-protected + LE Secure Connections; the pedal "displays" a fixed passkey
    // (it is shown in the app over USB and printed in the manual), the phone types it.
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityPasskey(passkey);
  }

  s_server = NimBLEDevice::createServer();
  s_server->setCallbacks(new ServerCallbacks());
  s_server->advertiseOnDisconnect(true);

  uint32_t rxProps = NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR;
  uint32_t txProps = NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ;
  if (s_secured) {
    rxProps |= NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN;
    txProps |= NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN;
  }

  NimBLEService *svc = s_server->createService(BLE_SERVICE_UUID);
  NimBLECharacteristic *rx = svc->createCharacteristic(BLE_RX_UUID, rxProps);
  rx->setCallbacks(new RxCallbacks());
  s_tx = svc->createCharacteristic(BLE_TX_UUID, txProps);

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setName(deviceName);
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->enableScanResponse(true);
  adv->start();

  protocolRegisterLink(&s_link);
  LOG("ble advertising as '%s' (%s, %d bond(s))", deviceName, s_secured ? "passkey required" : "open", NimBLEDevice::getNumBonds());
}

bool bleConnected()  { return s_connected; }
int  bleBondCount()  { return NimBLEDevice::getNumBonds(); }
void bleForgetBonds() { NimBLEDevice::deleteAllBonds(); }

void bleLoop() {
  // Move complete lines out from under the NimBLE task and handle them here.
  String lines;
  xSemaphoreTake(s_rxMutex, portMAX_DELAY);
  if (s_rxLines.length()) { lines = s_rxLines; s_rxLines = ""; }
  xSemaphoreGive(s_rxMutex);
  int start = 0;
  while (start < (int)lines.length()) {
    int nl = lines.indexOf('\n', start);
    if (nl < 0) break;
    String line = lines.substring(start, nl);
    start = nl + 1;
    if (line.length() && line[0] == '{') protocolHandleLine(line, &s_link);
  }
}
