#include "protocol.h"
#include "pins.h"
#include "config_model.h"
#include "controller.h"
#include "midi_router.h"
#include "net_midi.h"
#include "ble_service.h"
#include "presets.h"
#include "ota.h"
#include <ArduinoJson.h>
#include <memory>

static Link    *s_links[4];
static int      s_linkCount = 0;
static uint32_t s_rebootAt = 0;

void protocolBegin() { s_linkCount = 0; }

void protocolRegisterLink(Link *link) {
  if (s_linkCount < 4) s_links[s_linkCount++] = link;
}

void protocolBroadcast(const String &line) {
  for (int i = 0; i < s_linkCount; i++) s_links[i]->sendLine(line);
}

static void broadcastExcept(const String &line, Link *except) {
  for (int i = 0; i < s_linkCount; i++) if (s_links[i] != except) s_links[i]->sendLine(line);
}

static void reply(Link *to, JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  to->sendLine(out);
}
static void replyOk(Link *to, const char *cmd) {
  JsonDocument doc;
  doc["ev"] = "ok";
  doc["cmd"] = cmd;
  reply(to, doc);
}
static void replyError(Link *to, const char *msg) {
  JsonDocument doc;
  doc["ev"] = "error";
  doc["msg"] = msg;
  reply(to, doc);
}

static void fillInfo(JsonObject o) {
  o["fw"] = FW_VERSION;
  o["proto"] = PROTOCOL_VERSION;
  o["name"] = g_config.name;
  o["usb"] = midiUsbMounted();
  o["eth"] = netLinkUp();
  o["ip"] = netIp();
  o["sessions"] = netSessions();
  o["ble"] = bleConnected();
  o["bonds"] = bleBondCount();
  o["secured"] = g_config.blePin != 0;
  o["page"] = controllerPage();
  o["pages"] = g_config.pageCount;
  o["uptime"] = millis() / 1000;
  o["heap"] = ESP.getFreeHeap();
}

static void notifyConfigChanged(Link *from) {
  JsonDocument ev;
  ev["ev"] = "configChanged";
  ev["from"] = from->name();
  String out;
  serializeJson(ev, out);
  broadcastExcept(out, from);
}

void protocolHandleLine(const String &line, Link *from) {
  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, line);
  if (de) { replyError(from, "bad json"); return; }
  const char *cmd = doc["cmd"] | (const char *)nullptr;
  if (!cmd) { replyError(from, "cmd missing"); return; }

  // ---- status -------------------------------------------------------------------------
  if (!strcmp(cmd, "hello") || !strcmp(cmd, "info")) {
    JsonDocument r;
    fillInfo(r.to<JsonObject>());
    r["ev"] = cmd;
    reply(from, r);
  }
  // ---- configuration ------------------------------------------------------------------
  else if (!strcmp(cmd, "get")) {
    JsonDocument r;
    r["ev"] = "config";
    configToJson(g_config, r["config"].to<JsonObject>());
    reply(from, r);
  } else if (!strcmp(cmd, "set")) {
    String err;
    std::unique_ptr<DeviceConfig> next(new DeviceConfig);
    if (!configFromJson(doc["config"].as<JsonObjectConst>(), *next, err)) { replyError(from, err.c_str()); return; }
    bool securityChanged = next->blePin != g_config.blePin || strcmp(next->name, g_config.name) != 0;
    g_config = *next;
    configSave();
    controllerConfigChanged();
    JsonDocument r;
    r["ev"] = "ok";
    r["cmd"] = "set";
    r["rebootNeeded"] = securityChanged;   // BLE name / passkey apply after a reboot
    reply(from, r);
    notifyConfigChanged(from);
  } else if (!strcmp(cmd, "reset")) {
    configFactoryReset();
    controllerConfigChanged();
    replyOk(from, "reset");
    notifyConfigChanged(from);
  }
  // ---- live control -------------------------------------------------------------------
  else if (!strcmp(cmd, "page")) {
    int idx = doc["index"] | -1;
    if (!controllerSetPage(idx)) { replyError(from, "bad page"); return; }
    replyOk(from, "page");
  } else if (!strcmp(cmd, "press") || !strcmp(cmd, "release")) {
    int i = doc["i"] | -1;
    if (i < 0 || i >= NUM_SWITCHES) { replyError(from, "bad switch"); return; }
    controllerSimulate((uint8_t)i, cmd[0] == 'p');
    replyOk(from, cmd);
  } else if (!strcmp(cmd, "midi")) {
    const char *type = doc["type"] | "cc";
    uint8_t ch = doc["ch"] | g_config.midiChannel;
    uint8_t n = doc["n"] | 0;
    uint8_t v = doc["v"] | 0;
    uint8_t route = (uint8_t)(doc["route"] | (int)PORT_ALL) & PORT_ALL;
    if (!strcmp(type, "cc")) midiRouterSend(midiCC(ch, n, v), route);
    else if (!strcmp(type, "noteon")) midiRouterSend(midiNoteOn(ch, n, v), route);
    else if (!strcmp(type, "noteoff")) midiRouterSend(midiNoteOff(ch, n, v), route);
    else if (!strcmp(type, "pc")) midiRouterSend(midiProgramChange(ch, n), route);
    else { replyError(from, "bad midi type"); return; }
    replyOk(from, "midi");
  } else if (!strcmp(cmd, "state")) {
    // Snapshot of latch / bank state so a freshly connected app can draw it.
    JsonDocument r;
    r["ev"] = "state";
    r["page"] = controllerPage();
    JsonArray latched = r["latched"].to<JsonArray>();
    for (int p = 0; p < g_config.pageCount; p++)
      for (int i = 0; i < NUM_SWITCHES; i++)
        if (controllerLatched(p, i)) { JsonObject o = latched.add<JsonObject>(); o["page"] = p; o["i"] = i; }
    JsonArray banks = r["banks"].to<JsonArray>();
    for (int c = 1; c <= 16; c++) {
      int v = controllerBankValue(c);
      if (v >= 0) { JsonObject o = banks.add<JsonObject>(); o["ch"] = c; o["value"] = v; }
    }
    reply(from, r);
  }
  // ---- presets ------------------------------------------------------------------------
  else if (!strcmp(cmd, "presets")) {
    JsonDocument r;
    r["ev"] = "presets";
    presetsList(r["items"].to<JsonArray>());
    reply(from, r);
  } else if (!strcmp(cmd, "presetSave")) {
    String err;
    if (!presetsSave(doc["slot"] | -1, doc["name"] | "", err)) { replyError(from, err.c_str()); return; }
    replyOk(from, cmd);
  } else if (!strcmp(cmd, "presetLoad")) {
    String err;
    if (!presetsLoad(doc["slot"] | -1, err)) { replyError(from, err.c_str()); return; }
    replyOk(from, cmd);
    notifyConfigChanged(from);
  } else if (!strcmp(cmd, "presetDelete")) {
    String err;
    if (!presetsDelete(doc["slot"] | -1, err)) { replyError(from, err.c_str()); return; }
    replyOk(from, cmd);
  }
  // ---- BLE ----------------------------------------------------------------------------
  else if (!strcmp(cmd, "unpair")) {
    bleForgetBonds();
    replyOk(from, cmd);
  }
  // ---- firmware update ----------------------------------------------------------------
  else if (!strcmp(cmd, "otaBegin")) {
    String err;
    if (!otaBegin(doc["size"] | 0UL, doc["md5"] | (const char *)nullptr, err)) { replyError(from, err.c_str()); return; }
    replyOk(from, cmd);
  } else if (!strcmp(cmd, "otaData")) {
    const char *d = doc["d"] | (const char *)nullptr;
    String err;
    if (!d || !otaWriteBase64(d, strlen(d), err)) { replyError(from, err.length() ? err.c_str() : "data missing"); return; }
    JsonDocument r;
    r["ev"] = "ok";
    r["cmd"] = cmd;
    r["got"] = otaReceived();
    reply(from, r);
  } else if (!strcmp(cmd, "otaEnd")) {
    String err;
    if (!otaEnd(err)) { replyError(from, err.c_str()); return; }
    replyOk(from, cmd);
    s_rebootAt = millis() + 500;
  } else if (!strcmp(cmd, "otaAbort")) {
    otaAbort();
    replyOk(from, cmd);
  }
  // ---- misc ---------------------------------------------------------------------------
  else if (!strcmp(cmd, "reboot")) {
    replyOk(from, "reboot");
    s_rebootAt = millis() + 200;
  } else {
    replyError(from, "unknown cmd");
  }
}

void protocolLoop() {
  bool up;
  if (netTakeLinkEvent(up)) {
    JsonDocument ev;
    ev["ev"] = "link";
    ev["eth"] = up;
    ev["ip"] = netIp();
    String out;
    serializeJson(ev, out);
    protocolBroadcast(out);
  }
  if (s_rebootAt && (int32_t)(millis() - s_rebootAt) >= 0) {
    delay(50);
    ESP.restart();
  }
}
