// mod_wifi_creds.h — runtime WiFi and ESP-NOW credential management.
//
// Credentials are stored in NVS namespace "wifi_creds".
// First boot (or after wifi_creds_reset()) seeds NVS from secrets.h defines.
// Call wifi_creds_setup() before webui_init() and before bus_init().
//
// Runtime changes arrive via blob transfer (BLOB_NS_WIFI) — see mod_blob.h.
// The HTTP handler in webui.cpp also calls the setters directly.

#pragma once
#include <stdint.h>
#include <stddef.h>
#include "bus.h"

#define WIFI_CREDS_NVS_NS "wifi_creds"
#define WIFI_SSID_MAX 33   // 32 chars + null
#define WIFI_PASS_MAX 65   // 64 chars + null

void wifi_creds_setup();    // load NVS; seed from secrets.h on first boot

void wifi_creds_get_ssid(char* buf, size_t len);
void wifi_creds_get_pass(char* buf, size_t len);
void wifi_creds_get_pmk(uint8_t pmk[16]);
void wifi_creds_get_lmk(uint8_t lmk[16]);

void wifi_creds_set_ssid(const char* ssid);
void wifi_creds_set_pass(const char* pass);
void wifi_creds_set_pmk(const uint8_t pmk[16]);
void wifi_creds_set_lmk(const uint8_t lmk[16]);

void wifi_creds_save();    // persist RAM state to NVS
void wifi_creds_reset();   // clear NVS and reload secrets.h defaults

// Send all 4 credential fields as blob transfers. target = 0xFF for broadcast.
// flags are passed to BLOB_COMMIT (typically BLOB_FLAG_PERSIST).
// Caller is responsible for its own save and restart.
void wifi_creds_broadcast(uint8_t target, uint8_t flags);

// Called by the blob commit callback for BLOB_NS_WIFI frames.
void wifi_creds_on_blob(uint8_t key, const uint8_t* data, uint16_t len, uint8_t flags);
