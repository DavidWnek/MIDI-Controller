// Newline-delimited JSON control protocol, shared by BLE and USB serial.
#pragma once
#include <Arduino.h>

class Link {
 public:
  virtual ~Link() {}
  virtual void sendLine(const String &line) = 0;   // line has no trailing newline
  virtual const char *name() const = 0;
};

void protocolBegin();
void protocolRegisterLink(Link *link);
void protocolHandleLine(const String &line, Link *from);
void protocolBroadcast(const String &line);   // unsolicited event to every link
void protocolLoop();
