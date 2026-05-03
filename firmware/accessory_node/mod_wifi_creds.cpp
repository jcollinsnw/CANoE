#include "mod_wifi_creds.h"
#include "can_protocol.h"
#include "mod_blob.h"
#include <Preferences.h>
#include <Arduino.h>
#include <string.h>
#include "secrets.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

static char    g_ssid[WIFI_SSID_MAX] = {};
static char    g_pass[WIFI_PASS_MAX] = {};
static uint8_t g_pmk[16] = {};
static uint8_t g_lmk[16] = {};

void wifi_creds_setup() {
  Preferences p;
  p.begin(WIFI_CREDS_NVS_NS, true);
  bool has_ssid = p.isKey("ssid");
  p.end();

  if (!has_ssid) {
    strncpy(g_ssid, AP_SSID,     WIFI_SSID_MAX - 1);
    strncpy(g_pass, AP_PASSWORD, WIFI_PASS_MAX - 1);
    memcpy(g_pmk, (const uint8_t*)ESPNOW_PMK, 16);
    memcpy(g_lmk, (const uint8_t*)ESPNOW_LMK, 16);
    wifi_creds_save();
    Serial.println("[wcreds] initialized from secrets.h");
  } else {
    p.begin(WIFI_CREDS_NVS_NS, true);
    p.getString("ssid", g_ssid, WIFI_SSID_MAX);
    p.getString("pass", g_pass, WIFI_PASS_MAX);
    if (p.getBytesLength("pmk") == 16) p.getBytes("pmk", g_pmk, 16);
    else memcpy(g_pmk, (const uint8_t*)ESPNOW_PMK, 16);
    if (p.getBytesLength("lmk") == 16) p.getBytes("lmk", g_lmk, 16);
    else memcpy(g_lmk, (const uint8_t*)ESPNOW_LMK, 16);
    p.end();
    Serial.printf("[wcreds] loaded from NVS: ssid=\"%s\"\n", g_ssid);
  }
}

void wifi_creds_get_ssid(char* buf, size_t len) { strncpy(buf, g_ssid, len - 1); buf[len-1] = '\0'; }
void wifi_creds_get_pass(char* buf, size_t len) { strncpy(buf, g_pass, len - 1); buf[len-1] = '\0'; }
void wifi_creds_get_pmk(uint8_t pmk[16])        { memcpy(pmk, g_pmk, 16); }
void wifi_creds_get_lmk(uint8_t lmk[16])        { memcpy(lmk, g_lmk, 16); }

void wifi_creds_set_ssid(const char* ssid) { strncpy(g_ssid, ssid, WIFI_SSID_MAX - 1); g_ssid[WIFI_SSID_MAX-1] = '\0'; }
void wifi_creds_set_pass(const char* pass) { strncpy(g_pass, pass, WIFI_PASS_MAX - 1); g_pass[WIFI_PASS_MAX-1] = '\0'; }
void wifi_creds_set_pmk(const uint8_t pmk[16]) { memcpy(g_pmk, pmk, 16); }
void wifi_creds_set_lmk(const uint8_t lmk[16]) { memcpy(g_lmk, lmk, 16); }

void wifi_creds_save() {
  Preferences p; p.begin(WIFI_CREDS_NVS_NS, false);
  p.putString("ssid", g_ssid);
  p.putString("pass", g_pass);
  p.putBytes("pmk", g_pmk, 16);
  p.putBytes("lmk", g_lmk, 16);
  p.end();
  Serial.println("[wcreds] saved to NVS");
}

void wifi_creds_reset() {
  Preferences p; p.begin(WIFI_CREDS_NVS_NS, false); p.clear(); p.end();
  memset(g_ssid, 0, sizeof(g_ssid));
  memset(g_pass, 0, sizeof(g_pass));
  wifi_creds_setup();
  wlogln("[wcreds] reset to secrets.h defaults");
}

void wifi_creds_broadcast(uint8_t target, uint8_t flags) {
  blob_send(target, BLOB_NS_WIFI, BLOB_KEY_SSID, (const uint8_t*)g_ssid, strlen(g_ssid), flags);
  blob_send(target, BLOB_NS_WIFI, BLOB_KEY_PASS, (const uint8_t*)g_pass, strlen(g_pass), flags);
  blob_send(target, BLOB_NS_WIFI, BLOB_KEY_PMK,  g_pmk, 16, flags);
  blob_send(target, BLOB_NS_WIFI, BLOB_KEY_LMK,  g_lmk, 16, flags);
}

void wifi_creds_on_blob(uint8_t key, const uint8_t* data, uint16_t len, uint8_t flags) {
  switch (key) {
    case BLOB_KEY_SSID: {
      char s[WIFI_SSID_MAX] = {};
      size_t n = (len < WIFI_SSID_MAX - 1) ? len : WIFI_SSID_MAX - 1;
      memcpy(s, data, n);
      wifi_creds_set_ssid(s);
      break;
    }
    case BLOB_KEY_PASS: {
      char s[WIFI_PASS_MAX] = {};
      size_t n = (len < WIFI_PASS_MAX - 1) ? len : WIFI_PASS_MAX - 1;
      memcpy(s, data, n);
      wifi_creds_set_pass(s);
      break;
    }
    case BLOB_KEY_PMK:
      if (len == 16) wifi_creds_set_pmk(data);
      break;
    case BLOB_KEY_LMK:
      if (len == 16) wifi_creds_set_lmk(data);
      break;
    default: return;
  }
  wlog("[wcreds] blob key=0x%02X len=%u\n", key, len);
  if (flags & BLOB_FLAG_PERSIST) wifi_creds_save();
  if (flags & BLOB_FLAG_REBOOT)  { delay(100); ESP.restart(); }
}
