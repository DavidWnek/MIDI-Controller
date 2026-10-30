#include "config_model.h"
#include <LittleFS.h>
#include <memory>

DeviceConfig g_config;
static const char *CONFIG_PATH = "/config.json";

static const uint32_t DEFAULT_COLORS[NUM_SWITCHES] = {
  0xFF4040, 0xFF9000, 0xFFE000, 0x40FF40, 0x00E0C0, 0x4080FF, 0x9040FF, 0xFF40C0, 0xFFFFFF,
};

const char *switchModeName(SwitchMode m) {
  switch (m) {
    case MODE_LATCH: return "latch";
    case MODE_PAGE:  return "page";
    case MODE_RADIO: return "radio";
    case MODE_BANK:  return "bank";
    default:         return "momentary";
  }
}
const char *msgTypeName(MsgType t) {
  switch (t) { case TYPE_NOTE: return "note"; case TYPE_PC: return "pc"; default: return "cc"; }
}
const char *actionTypeName(ActionType t) {
  switch (t) {
    case ACT_NOTE_ON:  return "noteon";
    case ACT_NOTE_OFF: return "noteoff";
    case ACT_PC:       return "pc";
    case ACT_PAGE:     return "page";
    case ACT_PRESET:   return "preset";
    default:           return "cc";
  }
}
uint8_t configPortIndex(uint8_t portBit) {
  return portBit == PORT_DIN ? 1 : portBit == PORT_NET ? 2 : 0;
}

static bool parseMode(const char *s, SwitchMode &m) {
  if (!s) return false;
  if (!strcmp(s, "momentary")) { m = MODE_MOMENTARY; return true; }
  if (!strcmp(s, "latch"))     { m = MODE_LATCH;     return true; }
  if (!strcmp(s, "page"))      { m = MODE_PAGE;      return true; }
  if (!strcmp(s, "radio"))     { m = MODE_RADIO;     return true; }
  if (!strcmp(s, "bank"))      { m = MODE_BANK;      return true; }
  return false;
}
static bool parseType(const char *s, MsgType &t) {
  if (!s) return false;
  if (!strcmp(s, "cc"))   { t = TYPE_CC;   return true; }
  if (!strcmp(s, "note")) { t = TYPE_NOTE; return true; }
  if (!strcmp(s, "pc"))   { t = TYPE_PC;   return true; }
  return false;
}
static bool parseActionType(const char *s, ActionType &t) {
  if (!s) return false;
  if (!strcmp(s, "cc"))      { t = ACT_CC;       return true; }
  if (!strcmp(s, "noteon"))  { t = ACT_NOTE_ON;  return true; }
  if (!strcmp(s, "noteoff")) { t = ACT_NOTE_OFF; return true; }
  if (!strcmp(s, "pc"))      { t = ACT_PC;       return true; }
  if (!strcmp(s, "page"))    { t = ACT_PAGE;     return true; }
  if (!strcmp(s, "preset"))  { t = ACT_PRESET;   return true; }
  return false;
}
static void copyStr(char *dst, size_t cap, const char *src, const char *fallback) {
  if (!src || !*src) src = fallback;
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = 0;
}
static uint32_t parseColor(const char *s, uint32_t fallback) {
  if (!s) return fallback;
  if (*s == '#') s++;
  if (strlen(s) != 6) return fallback;
  return (uint32_t)strtoul(s, nullptr, 16) & 0xFFFFFF;
}
static long clampL(long v, long lo, long hi) {
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return v;
}

void configSetDefaults(DeviceConfig &c) {
  memset(&c, 0, sizeof(c));
  copyStr(c.name, sizeof(c.name), "MidiPedal", "MidiPedal");
  c.midiChannel = 1;
  c.startPage = 0;
  c.brightness = 40;
  c.pageCount = 1;
  c.thru[0] = PORT_DIN;   // USB in  -> DIN out  (acts as a USB-MIDI interface)
  c.thru[1] = PORT_USB;   // DIN in  -> USB out
  c.thru[2] = 0;          // network in -> nothing
  c.blePin = 0;
  for (int p = 0; p < MAX_PAGES; p++) {
    snprintf(c.pages[p].name, sizeof(c.pages[p].name), "Page %d", p + 1);
    for (int i = 0; i < NUM_SWITCHES; i++) {
      SwitchConfig &s = c.pages[p].sw[i];
      snprintf(s.name, sizeof(s.name), "SW%d", i + 1);
      s.mode = MODE_MOMENTARY;
      s.type = TYPE_CC;
      s.channel = 0;
      s.number = 20 + i;
      s.onValue = 127;
      s.offValue = 0;
      s.pageTarget = PAGE_NEXT;
      s.color = DEFAULT_COLORS[i];
      s.route = PORT_ALL;
      s.group = 0;
      s.bankStep = 1;
      s.bankMin = 0;
      s.bankMax = 127;
      s.bankWrap = true;
      s.extra.count = s.hold.count = s.tap2.count = 0;
    }
  }
}

static void actionsToJson(const ActionList &l, JsonArray out) {
  for (int i = 0; i < l.count && i < MAX_ACTIONS; i++) {
    JsonObject o = out.add<JsonObject>();
    o["t"] = actionTypeName(l.a[i].t);
    o["ch"] = l.a[i].ch;
    o["n"] = l.a[i].n;
    o["v"] = l.a[i].v;
  }
}

static bool actionsFromJson(JsonArrayConst in, ActionList &l, String &err) {
  l.count = 0;
  if (in.isNull()) return true;
  for (JsonObjectConst o : in) {
    if (l.count >= MAX_ACTIONS) { err = "too many actions"; return false; }
    MidiAction &a = l.a[l.count];
    if (!parseActionType(o["t"] | "cc", a.t)) { err = "bad action type"; return false; }
    a.ch = (uint8_t)clampL(o["ch"] | 0, 0, 16);
    long n = o["n"] | 0;
    a.n = (int8_t)clampL(n, a.t == ACT_PAGE ? PAGE_NEXT : 0, 127);
    a.v = (uint8_t)clampL(o["v"] | 0, 0, 127);
    l.count++;
  }
  return true;
}

void configToJson(const DeviceConfig &c, JsonObject out) {
  out["v"] = PROTOCOL_VERSION;
  out["name"] = c.name;
  out["ch"] = c.midiChannel;
  out["startPage"] = c.startPage;
  out["brightness"] = c.brightness;
  out["blePin"] = c.blePin;
  JsonObject thru = out["thru"].to<JsonObject>();
  thru["usb"] = c.thru[0];
  thru["din"] = c.thru[1];
  thru["net"] = c.thru[2];
  JsonArray pages = out["pages"].to<JsonArray>();
  for (int p = 0; p < c.pageCount; p++) {
    JsonObject po = pages.add<JsonObject>();
    po["name"] = c.pages[p].name;
    JsonArray sws = po["sw"].to<JsonArray>();
    for (int i = 0; i < NUM_SWITCHES; i++) {
      const SwitchConfig &s = c.pages[p].sw[i];
      JsonObject so = sws.add<JsonObject>();
      char col[8];
      snprintf(col, sizeof(col), "#%06lX", (unsigned long)(s.color & 0xFFFFFF));
      so["name"] = s.name;
      so["mode"] = switchModeName(s.mode);
      so["type"] = msgTypeName(s.type);
      so["ch"] = s.channel;
      so["num"] = s.number;
      so["on"] = s.onValue;
      so["off"] = s.offValue;
      so["page"] = s.pageTarget;
      so["color"] = col;
      so["route"] = s.route;
      so["group"] = s.group;
      JsonObject b = so["bank"].to<JsonObject>();
      b["step"] = s.bankStep;
      b["min"] = s.bankMin;
      b["max"] = s.bankMax;
      b["wrap"] = s.bankWrap;
      actionsToJson(s.extra, so["extra"].to<JsonArray>());
      actionsToJson(s.hold, so["hold"].to<JsonArray>());
      actionsToJson(s.tap2, so["tap2"].to<JsonArray>());
    }
  }
}

bool configFromJson(JsonObjectConst in, DeviceConfig &c, String &err) {
  // DeviceConfig is several KB: keep it off the task stack.
  std::unique_ptr<DeviceConfig> np(new DeviceConfig);
  DeviceConfig &n = *np;
  configSetDefaults(n);
  if (in.isNull()) { err = "config missing"; return false; }

  copyStr(n.name, sizeof(n.name), in["name"] | (const char *)nullptr, "MidiPedal");
  n.midiChannel = (uint8_t)clampL(in["ch"] | 1, 1, 16);
  n.brightness = (uint8_t)clampL(in["brightness"] | 40, 0, 255);
  n.blePin = (uint32_t)clampL(in["blePin"] | 0L, 0, 999999);
  JsonObjectConst thru = in["thru"].as<JsonObjectConst>();
  if (!thru.isNull()) {
    n.thru[0] = (uint8_t)(thru["usb"] | (int)PORT_DIN) & PORT_ALL;
    n.thru[1] = (uint8_t)(thru["din"] | (int)PORT_USB) & PORT_ALL;
    n.thru[2] = (uint8_t)(thru["net"] | 0) & PORT_ALL;
  }

  JsonArrayConst pages = in["pages"].as<JsonArrayConst>();
  if (pages.isNull() || pages.size() == 0) { err = "pages missing"; return false; }
  if (pages.size() > MAX_PAGES) { err = "too many pages"; return false; }
  n.pageCount = pages.size();
  n.startPage = (uint8_t)clampL(in["startPage"] | 0, 0, n.pageCount - 1);

  int p = 0;
  for (JsonObjectConst po : pages) {
    PageConfig &pg = n.pages[p];
    char fallback[17];
    snprintf(fallback, sizeof(fallback), "Page %d", p + 1);
    copyStr(pg.name, sizeof(pg.name), po["name"] | (const char *)nullptr, fallback);
    JsonArrayConst sws = po["sw"].as<JsonArrayConst>();
    int i = 0;
    for (JsonObjectConst so : sws) {
      if (i >= NUM_SWITCHES) break;
      SwitchConfig &s = pg.sw[i];
      char swfallback[8];
      snprintf(swfallback, sizeof(swfallback), "SW%d", i + 1);
      copyStr(s.name, sizeof(s.name), so["name"] | (const char *)nullptr, swfallback);
      if (!parseMode(so["mode"] | "momentary", s.mode)) { err = "bad mode"; return false; }
      if (!parseType(so["type"] | "cc", s.type)) { err = "bad type"; return false; }
      s.channel = (uint8_t)clampL(so["ch"] | 0, 0, 16);
      s.number = (uint8_t)clampL(so["num"] | 0, 0, 127);
      s.onValue = (uint8_t)clampL(so["on"] | 127, 0, 127);
      s.offValue = (uint8_t)clampL(so["off"] | 0, 0, 127);
      long pt = so["page"] | (long)PAGE_NEXT;
      s.pageTarget = (int8_t)clampL(pt, PAGE_NEXT, n.pageCount - 1);
      s.color = parseColor(so["color"] | (const char *)nullptr, DEFAULT_COLORS[i]);
      s.route = (uint8_t)(so["route"] | (int)PORT_ALL) & PORT_ALL;
      s.group = (uint8_t)clampL(so["group"] | 0, 0, 8);
      JsonObjectConst b = so["bank"].as<JsonObjectConst>();
      s.bankStep = (int8_t)clampL(b["step"] | 1, -64, 64);
      s.bankMin = (uint8_t)clampL(b["min"] | 0, 0, 127);
      s.bankMax = (uint8_t)clampL(b["max"] | 127, 0, 127);
      if (s.bankMax < s.bankMin) s.bankMax = s.bankMin;
      s.bankWrap = b["wrap"] | true;
      if (!actionsFromJson(so["extra"].as<JsonArrayConst>(), s.extra, err)) return false;
      if (!actionsFromJson(so["hold"].as<JsonArrayConst>(), s.hold, err)) return false;
      if (!actionsFromJson(so["tap2"].as<JsonArrayConst>(), s.tap2, err)) return false;
      i++;
    }
    p++;
  }
  c = n;
  return true;
}

bool configBegin() {
  if (!LittleFS.begin(true)) {
    LOG("LittleFS mount failed");
    return false;
  }
  return true;
}

bool configLoad() {
  configSetDefaults(g_config);
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) {
    LOG("no config, using defaults");
    return false;
  }
  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, f);
  f.close();
  if (de) {
    LOG("config parse error: %s", de.c_str());
    return false;
  }
  String err;
  if (!configFromJson(doc.as<JsonObjectConst>(), g_config, err)) {
    LOG("config invalid: %s", err.c_str());
    configSetDefaults(g_config);
    return false;
  }
  LOG("config loaded: %d page(s)", g_config.pageCount);
  return true;
}

bool configSave() {
  JsonDocument doc;
  configToJson(g_config, doc.to<JsonObject>());
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) {
    LOG("config open for write failed");
    return false;
  }
  size_t n = serializeJson(doc, f);
  f.close();
  LOG("config saved (%u bytes)", (unsigned)n);
  return n > 0;
}

bool configFactoryReset() {
  configSetDefaults(g_config);
  LittleFS.remove(CONFIG_PATH);
  return configSave();
}
