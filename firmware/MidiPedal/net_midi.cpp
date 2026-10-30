#include "net_midi.h"
#include "pins.h"
#include <Network.h>
#include <ETH.h>
#include <SPI.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <AppleMIDI.h>

APPLEMIDI_CREATE_INSTANCE(NetworkUDP, MIDI_NET, "MidiPedal", DEFAULT_CONTROL_PORT);

static volatile bool s_linkUp = false;
static volatile bool s_linkEvent = false;
static volatile int  s_sessions = 0;
static bool          s_midiStarted = false;
static bool          s_otaStarted = false;
static String        s_host;
static String        s_otaPassword;

static void startSession() {
  if (s_midiStarted) return;
  MIDI_NET.begin(MIDI_CHANNEL_OMNI);
  MIDI_NET.turnThruOff();
  s_midiStarted = true;
}
static void stopSession() {
  if (!s_midiStarted) return;
  AppleMIDI_NET.end();
  s_midiStarted = false;
  s_sessions = 0;
}

static void onNetEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname(s_host.c_str());
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      if (!s_otaStarted) {
        // ArduinoOTA starts mDNS for us (hostname + _arduino._tcp); we add the MIDI service.
        ArduinoOTA.setHostname(s_host.c_str());
        if (s_otaPassword.length()) ArduinoOTA.setPassword(s_otaPassword.c_str());
        ArduinoOTA.setMdnsEnabled(true);
        ArduinoOTA.begin();
        s_otaStarted = true;
      } else {
        MDNS.end();
        MDNS.begin(s_host.c_str());
      }
      MDNS.addService("_apple-midi", "_udp", DEFAULT_CONTROL_PORT);
      startSession();
      s_linkUp = true;
      s_linkEvent = true;
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      stopSession();
      if (s_linkUp) s_linkEvent = true;
      s_linkUp = false;
      break;
    default:
      break;
  }
}

void netMidiBegin(const char *deviceName, uint32_t otaPassword) {
  // mDNS host: lower-case, letters/digits/dashes only.
  s_host = "";
  for (const char *p = deviceName; *p; p++) {
    char c = *p;
    if (isalnum((unsigned char)c)) s_host += (char)tolower((unsigned char)c);
    else if (c == ' ' || c == '-' || c == '_') s_host += '-';
  }
  if (s_host.isEmpty()) s_host = "midipedal";
  s_otaPassword = otaPassword ? String(otaPassword) : String("");

  AppleMIDI_NET.setName(deviceName);
  AppleMIDI_NET.setHandleConnected([](const APPLEMIDI_NAMESPACE::ssrc_t &, const char *name) {
    s_sessions = s_sessions + 1;
    LOG("rtp-midi session joined: %s", name);
  });
  AppleMIDI_NET.setHandleDisconnected([](const APPLEMIDI_NAMESPACE::ssrc_t &) {
    if (s_sessions > 0) s_sessions = s_sessions - 1;
    LOG("rtp-midi session left");
  });

  Network.onEvent(onNetEvent);
  bool ok = ETH.begin(ETH_PHY_W5500, W5500_PHY_ADDR, W5500_CS, W5500_INT, W5500_RST,
                      SPI2_HOST, W5500_SCLK, W5500_MISO, W5500_MOSI, W5500_SPI_MHZ);
  LOG("ethernet %s", ok ? "started" : "FAILED to start");
}

void netMidiLoop() {
  if (s_otaStarted) ArduinoOTA.handle();
  if (!s_midiStarted) return;
  int guard = 16;
  while (guard-- && MIDI_NET.read()) {
    midi::MidiType t = MIDI_NET.getType();
    if (t < midi::NoteOff || t > midi::PitchBend) continue;   // channel voice only
    MidiMsg m;
    m.status = (uint8_t)t | (uint8_t)(MIDI_NET.getChannel() - 1);
    m.data1 = MIDI_NET.getData1();
    m.data2 = MIDI_NET.getData2();
    m.len = (t == midi::ProgramChange || t == midi::AfterTouchChannel) ? 2 : 3;
    midiRouterDispatchInput(PORT_NET, m);
  }
}

void netMidiSend(const MidiMsg &m) {
  if (!s_midiStarted || s_sessions <= 0) return;
  MIDI_NET.send((midi::MidiType)(m.status & 0xF0), m.data1, m.len > 2 ? m.data2 : 0, (m.status & 0x0F) + 1);
}

bool   netLinkUp()   { return s_linkUp; }
String netIp()       { return s_linkUp ? ETH.localIP().toString() : String(""); }
int    netSessions() { return s_sessions; }
bool netTakeLinkEvent(bool &up) {
  if (!s_linkEvent) return false;
  s_linkEvent = false;
  up = s_linkUp;
  return true;
}
