#include "controller.h"
#include "config_model.h"
#include "status_led.h"
#include "protocol.h"
#include "presets.h"
#include "net_midi.h"
#include <ArduinoJson.h>

// ---- runtime state ---------------------------------------------------------------------

enum SwState : uint8_t {
  ST_IDLE,
  ST_DOWN_PENDING,   // held, press deferred because a double tap is possible
  ST_DOWN_FIRED,     // held, press action already sent
  ST_DOWN_TAP2,      // held, this press was consumed as a double tap
  ST_WAIT_TAP2,      // released, waiting to see if a second tap follows
};

struct SwRuntime {
  SwState  st;
  uint8_t  page;       // page the press happened on
  uint32_t tDown;
  uint32_t tUp;
  bool     holdFired;
  bool     pendingPress;   // released before the double-tap window closed; press not yet sent
};

static uint8_t   s_page = 0;
static bool      s_latched[MAX_PAGES][NUM_SWITCHES];
static int16_t   s_bank[16];                     // per MIDI channel, -1 = not yet stepped
static SwRuntime s_rt[NUM_SWITCHES];
static int       s_pendingPreset = -1;
static bool      s_simDown[NUM_SWITCHES];       // presses simulated from the app
static uint32_t  s_simDownSince[NUM_SWITCHES];
static bool      s_simLongFired[NUM_SWITCHES];

static void emit(JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  protocolBroadcast(out);
}

static void emitLatch(uint8_t page, uint8_t i, bool on) {
  JsonDocument l;
  l["ev"] = "latch";
  l["page"] = page;
  l["i"] = i;
  l["on"] = on;
  emit(l);
}

static void emitBank(uint8_t ch, int value) {
  JsonDocument b;
  b["ev"] = "bank";
  b["ch"] = ch;
  b["value"] = value;
  emit(b);
}

static void updateIdleLed() {
  ledSetIdle(netLinkUp() ? 0x003010 : 0x000030);
}

static uint8_t resolveChannel(uint8_t ch) { return ch ? ch : g_config.midiChannel; }

// ---- lifecycle -------------------------------------------------------------------------

void controllerBegin() {
  memset(s_latched, 0, sizeof(s_latched));
  memset(s_rt, 0, sizeof(s_rt));
  for (int c = 0; c < 16; c++) s_bank[c] = -1;
  s_page = g_config.startPage < g_config.pageCount ? g_config.startPage : 0;
  midiRouterSetInputHandler(controllerOnMidiIn);
  updateIdleLed();
}

uint8_t controllerPage() { return s_page; }

bool controllerLatched(uint8_t page, uint8_t i) {
  return page < MAX_PAGES && i < NUM_SWITCHES && s_latched[page][i];
}

int controllerBankValue(uint8_t ch) {
  return (ch >= 1 && ch <= 16) ? s_bank[ch - 1] : -1;
}

bool controllerSetPage(int index) {
  int target = index;
  if (index == PAGE_NEXT) target = (s_page + 1) % g_config.pageCount;
  else if (index == PAGE_PREV) target = (s_page + g_config.pageCount - 1) % g_config.pageCount;
  if (target < 0 || target >= g_config.pageCount) return false;
  s_page = (uint8_t)target;
  JsonDocument ev;
  ev["ev"] = "page";
  ev["index"] = s_page;
  emit(ev);
  return true;
}

void controllerConfigChanged() {
  if (s_page >= g_config.pageCount) s_page = 0;
  memset(s_latched, 0, sizeof(s_latched));
  memset(s_rt, 0, sizeof(s_rt));
  JsonDocument ev;
  ev["ev"] = "page";
  ev["index"] = s_page;
  emit(ev);
}

// ---- actions ---------------------------------------------------------------------------

static void sendPrimary(const SwitchConfig &s, bool on) {
  uint8_t ch = resolveChannel(s.channel);
  switch (s.type) {
    case TYPE_CC:
      midiRouterSend(midiCC(ch, s.number, on ? s.onValue : s.offValue), s.route);
      break;
    case TYPE_NOTE:
      if (on) midiRouterSend(midiNoteOn(ch, s.number, s.onValue), s.route);
      else    midiRouterSend(midiNoteOff(ch, s.number, 0), s.route);
      break;
    case TYPE_PC:
      if (on) midiRouterSend(midiProgramChange(ch, s.number), s.route);
      break;
  }
}

static void runActions(const ActionList &list, uint8_t route) {
  for (int k = 0; k < list.count && k < MAX_ACTIONS; k++) {
    const MidiAction &a = list.a[k];
    uint8_t ch = resolveChannel(a.ch);
    switch (a.t) {
      case ACT_CC:       midiRouterSend(midiCC(ch, (uint8_t)a.n, a.v), route); break;
      case ACT_NOTE_ON:  midiRouterSend(midiNoteOn(ch, (uint8_t)a.n, a.v), route); break;
      case ACT_NOTE_OFF: midiRouterSend(midiNoteOff(ch, (uint8_t)a.n, a.v), route); break;
      case ACT_PC:       midiRouterSend(midiProgramChange(ch, (uint8_t)a.n), route); break;
      case ACT_PAGE:     controllerSetPage(a.n); break;
      case ACT_PRESET:   s_pendingPreset = a.n; break;   // applied from controllerLoop()
    }
  }
}

// Turn a radio switch on and the rest of its group off (sending their off form).
static void radioSelect(uint8_t page, uint8_t i) {
  const PageConfig &pg = g_config.pages[page];
  uint8_t group = pg.sw[i].group;
  for (int k = 0; k < NUM_SWITCHES; k++) {
    if (k == i) continue;
    const SwitchConfig &o = pg.sw[k];
    if (o.mode == MODE_RADIO && o.group == group && s_latched[page][k]) {
      s_latched[page][k] = false;
      sendPrimary(o, false);
      emitLatch(page, k, false);
    }
  }
  if (!s_latched[page][i]) {
    s_latched[page][i] = true;
    emitLatch(page, i, true);
  }
}

static void bankStep(const SwitchConfig &s) {
  uint8_t ch = resolveChannel(s.channel);
  int16_t &v = s_bank[ch - 1];
  int next = (v < 0) ? (int)s.bankMin : (int)v + s.bankStep;
  if (next > s.bankMax) next = s.bankWrap ? s.bankMin : s.bankMax;
  if (next < s.bankMin) next = s.bankWrap ? s.bankMax : s.bankMin;
  v = (int16_t)next;
  midiRouterSend(midiProgramChange(ch, (uint8_t)next), s.route);
  emitBank(ch, next);
}

static void doPress(uint8_t page, uint8_t i) {
  const SwitchConfig &s = g_config.pages[page].sw[i];
  switch (s.mode) {
    case MODE_MOMENTARY:
      sendPrimary(s, true);
      break;
    case MODE_LATCH: {
      bool on = !s_latched[page][i];
      s_latched[page][i] = on;
      sendPrimary(s, on);
      emitLatch(page, i, on);
      break;
    }
    case MODE_RADIO:
      radioSelect(page, i);      // deselect the previous one first (break before make)
      sendPrimary(s, true);
      break;
    case MODE_PAGE:
      controllerSetPage(s.pageTarget);
      break;
    case MODE_BANK:
      bankStep(s);
      break;
  }
  runActions(s.extra, s.route);
}

static void doRelease(uint8_t page, uint8_t i) {
  const SwitchConfig &s = g_config.pages[page].sw[i];
  if (s.mode == MODE_MOMENTARY) sendPrimary(s, false);
}

static void emitSwitch(uint8_t i, bool down) {
  JsonDocument e;
  e["ev"] = "switch";
  e["i"] = i;
  e["down"] = down;
  emit(e);
}

// ---- switch events ---------------------------------------------------------------------

void controllerOnSwitch(uint8_t i, SwitchEvent ev) {
  if (i >= NUM_SWITCHES) return;
  SwRuntime &r = s_rt[i];
  uint32_t now = millis();

  if (ev == SW_PRESS) {
    const SwitchConfig &cur = g_config.pages[s_page].sw[i];
    ledFlash(cur.color, 120);
    emitSwitch(i, true);
    bool canTap2 = cur.tap2.count > 0;
    if (canTap2 && r.st == ST_WAIT_TAP2 && (now - r.tUp) <= SWITCH_DOUBLETAP_MS && r.page == s_page) {
      r.pendingPress = false;          // the first tap's press is dropped in favour of tap2
      runActions(cur.tap2, cur.route);
      JsonDocument e;
      e["ev"] = "tap2";
      e["i"] = i;
      emit(e);
      r.st = ST_DOWN_TAP2;
    } else {
      r.page = s_page;
      r.st = canTap2 ? ST_DOWN_PENDING : ST_DOWN_FIRED;
      if (r.st == ST_DOWN_FIRED) doPress(r.page, i);
    }
    r.tDown = now;
    r.holdFired = false;
  } else if (ev == SW_RELEASE) {
    emitSwitch(i, false);
    switch (r.st) {
      case ST_DOWN_PENDING:
        // Short tap on a switch with a double-tap action: hold the press back until we
        // know whether a second tap follows.
        r.pendingPress = true;
        r.st = ST_WAIT_TAP2;
        break;
      case ST_DOWN_FIRED:
        doRelease(r.page, i);
        r.st = ST_IDLE;
        break;
      default:
        r.st = ST_IDLE;
        break;
    }
    r.tUp = now;
  } else if (ev == SW_LONG) {
    if (r.st == ST_DOWN_PENDING) {     // a long hold is clearly not a double tap
      doPress(r.page, i);
      r.st = ST_DOWN_FIRED;
    }
    const SwitchConfig &s = g_config.pages[r.page].sw[i];
    if (!r.holdFired && s.hold.count > 0) {
      r.holdFired = true;
      runActions(s.hold, s.route);
    }
    JsonDocument e;
    e["ev"] = "long";
    e["i"] = i;
    emit(e);
  }
  updateIdleLed();
}

void controllerLoop() {
  uint32_t now = millis();
  for (int i = 0; i < NUM_SWITCHES; i++) {
    SwRuntime &r = s_rt[i];
    if (r.st == ST_WAIT_TAP2 && (now - r.tUp) > SWITCH_DOUBLETAP_MS) {
      if (r.pendingPress) {            // it was a single tap after all: send press + release now
        r.pendingPress = false;
        doPress(r.page, i);
        doRelease(r.page, i);
      }
      r.st = ST_IDLE;
    }
    if (r.st == ST_DOWN_PENDING && (now - r.tDown) > SWITCH_DOUBLETAP_MS) {
      doPress(r.page, i);
      r.st = ST_DOWN_FIRED;
    }
  }
  for (int i = 0; i < NUM_SWITCHES; i++) {
    if (s_simDown[i] && !s_simLongFired[i] && (now - s_simDownSince[i]) >= SWITCH_LONGPRESS_MS) {
      s_simLongFired[i] = true;
      controllerOnSwitch(i, SW_LONG);
    }
  }
  if (s_pendingPreset >= 0) {
    int slot = s_pendingPreset;
    s_pendingPreset = -1;
    String err;
    if (presetsLoad(slot, err)) {
      JsonDocument ev;
      ev["ev"] = "configChanged";
      ev["from"] = "preset";
      ev["slot"] = slot;
      emit(ev);
    } else {
      LOG("preset %d: %s", slot, err.c_str());
    }
  }
}

void controllerSimulate(uint8_t i, bool down) {
  if (i >= NUM_SWITCHES) return;
  s_simDown[i] = down;
  s_simDownSince[i] = millis();
  s_simLongFired[i] = false;
  controllerOnSwitch(i, down ? SW_PRESS : SW_RELEASE);
}

// ---- incoming MIDI: keep latch / radio / bank state in step with the rig ------------------

void controllerOnMidiIn(uint8_t, const MidiMsg &m) {
  uint8_t kind = m.status & 0xF0;
  uint8_t ch = (m.status & 0x0F) + 1;
  if (kind != 0xB0 && kind != 0xC0) return;

  if (kind == 0xC0) {
    if (s_bank[ch - 1] != m.data1) {
      s_bank[ch - 1] = m.data1;
      emitBank(ch, m.data1);
    }
  }

  for (int p = 0; p < g_config.pageCount; p++) {
    for (int i = 0; i < NUM_SWITCHES; i++) {
      const SwitchConfig &s = g_config.pages[p].sw[i];
      if (s.mode != MODE_LATCH && s.mode != MODE_RADIO) continue;
      if (resolveChannel(s.channel) != ch || s.number != m.data1) continue;
      if (kind == 0xB0 && s.type != TYPE_CC) continue;
      if (kind == 0xC0 && s.type != TYPE_PC) continue;

      bool wantOn;
      if (kind == 0xC0) wantOn = true;
      else if (m.data2 == s.onValue) wantOn = true;
      else if (m.data2 == s.offValue) wantOn = false;
      else continue;

      if (s.mode == MODE_RADIO && wantOn) {
        // Mirror the selection silently: the rig already made the change.
        const PageConfig &pg = g_config.pages[p];
        for (int k = 0; k < NUM_SWITCHES; k++) {
          if (k != i && pg.sw[k].mode == MODE_RADIO && pg.sw[k].group == s.group && s_latched[p][k]) {
            s_latched[p][k] = false;
            emitLatch(p, k, false);
          }
        }
      }
      if (s_latched[p][i] != wantOn) {
        s_latched[p][i] = wantOn;
        emitLatch(p, i, wantOn);
      }
    }
  }
}
