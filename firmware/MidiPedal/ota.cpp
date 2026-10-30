#include "ota.h"
#include "pins.h"
#include <Update.h>
#include <mbedtls/base64.h>

static bool    s_active = false;
static size_t  s_received = 0;
static size_t  s_total = 0;
static uint8_t s_buf[6144];

bool otaBegin(size_t size, const char *md5, String &err) {
  if (s_active) Update.abort();
  s_active = false;
  s_received = 0;
  s_total = size;
  if (size < 1024) { err = "size too small"; return false; }
  if (!Update.begin(size, U_FLASH)) { err = Update.errorString(); return false; }
  if (md5 && strlen(md5) == 32 && !Update.setMD5(md5)) {
    Update.abort();
    err = "bad md5";
    return false;
  }
  s_active = true;
  LOG("ota begin: %u bytes", (unsigned)size);
  return true;
}

bool otaWriteBase64(const char *b64, size_t len, String &err) {
  if (!s_active) { err = "ota not started"; return false; }
  size_t olen = 0;
  int rc = mbedtls_base64_decode(s_buf, sizeof(s_buf), &olen, (const unsigned char *)b64, len);
  if (rc != 0) { err = rc == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL ? "chunk too large" : "bad base64"; return false; }
  if (olen == 0) return true;
  size_t w = Update.write(s_buf, olen);
  if (w != olen) {
    err = Update.errorString();
    Update.abort();
    s_active = false;
    return false;
  }
  s_received += olen;
  return true;
}

bool otaEnd(String &err) {
  if (!s_active) { err = "ota not started"; return false; }
  s_active = false;
  if (!Update.end(true)) { err = Update.errorString(); return false; }
  if (!Update.isFinished()) { err = "incomplete"; return false; }
  LOG("ota complete: %u bytes", (unsigned)s_received);
  return true;
}

void otaAbort() {
  if (s_active) Update.abort();
  s_active = false;
  s_received = 0;
}

bool   otaActive()   { return s_active; }
size_t otaReceived() { return s_received; }
size_t otaTotal()    { return s_total; }
