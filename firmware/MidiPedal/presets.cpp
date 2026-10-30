#include "presets.h"
#include "pins.h"
#include "config_model.h"
#include "controller.h"
#include <LittleFS.h>
#include <memory>

static String pathFor(int slot) { return String("/presets/") + slot + ".json"; }

static bool validSlot(int slot, String &err) {
  if (slot < 0 || slot >= MAX_PRESETS) { err = "bad slot"; return false; }
  return true;
}

void presetsList(JsonArray out) {
  for (int slot = 0; slot < MAX_PRESETS; slot++) {
    File f = LittleFS.open(pathFor(slot), "r");
    if (!f) continue;
    JsonObject o = out.add<JsonObject>();
    o["slot"] = slot;
    o["size"] = f.size();
    // Read just enough to pick the preset name out of the header.
    JsonDocument filter;
    filter["preset"] = true;
    JsonDocument doc;
    DeserializationError de = deserializeJson(doc, f, DeserializationOption::Filter(filter));
    f.close();
    o["name"] = de ? "?" : (const char *)(doc["preset"] | "Preset");
  }
}

bool presetsSave(int slot, const char *name, String &err) {
  if (!validSlot(slot, err)) return false;
  LittleFS.mkdir("/presets");
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["preset"] = (name && *name) ? name : (String("Preset ") + (slot + 1)).c_str();
  configToJson(g_config, root);
  File f = LittleFS.open(pathFor(slot), "w");
  if (!f) { err = "cannot write preset"; return false; }
  size_t n = serializeJson(doc, f);
  f.close();
  LOG("preset %d saved (%u bytes)", slot, (unsigned)n);
  return n > 0;
}

bool presetsLoad(int slot, String &err) {
  if (!validSlot(slot, err)) return false;
  File f = LittleFS.open(pathFor(slot), "r");
  if (!f) { err = "empty slot"; return false; }
  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, f);
  f.close();
  if (de) { err = de.c_str(); return false; }
  std::unique_ptr<DeviceConfig> next(new DeviceConfig);
  if (!configFromJson(doc.as<JsonObjectConst>(), *next, err)) return false;
  g_config = *next;
  configSave();
  controllerConfigChanged();
  LOG("preset %d loaded", slot);
  return true;
}

bool presetsDelete(int slot, String &err) {
  if (!validSlot(slot, err)) return false;
  if (!LittleFS.remove(pathFor(slot))) { err = "empty slot"; return false; }
  return true;
}
