// mod_relay.cpp — relay GPIO control, safety watchdog, NVS config, telemetry.

#include <Arduino.h>
#include <Preferences.h>
#include "node_config.h"

#ifdef ENABLE_RELAY

#include "can_protocol.h"
#include "bus.h"
#include "node_state.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

static const uint8_t  RELAY_PINS[NUM_RELAYS]    = RELAY_PINS_INIT;
static const uint16_t RELAY_DEF_MAX[NUM_RELAYS] = RELAY_MAX_ON_INIT;

static uint8_t  g_relay_state = 0;
static uint32_t g_relay_on_at[NUM_RELAYS];
static uint16_t g_max_on_ms[NUM_RELAYS];
static Preferences g_prefs;

// --------------------------------------------------------------
// Internal helpers
// --------------------------------------------------------------
static void relay_write(uint8_t idx, bool on) {
  if (idx >= NUM_RELAYS) return;
  digitalWrite(RELAY_PINS[idx], (on == RELAY_ACTIVE_HIGH) ? HIGH : LOW);
  if (on) {
    if (!(g_relay_state & (1 << idx))) g_relay_on_at[idx] = millis();
    g_relay_state |= (1 << idx);
  } else {
    g_relay_state &= ~(1 << idx);
  }
}

static void apply_relay_cmd(uint8_t mask, uint8_t desired) {
  for (uint8_t i = 0; i < NUM_RELAYS; i++)
    if (mask & (1 << i)) relay_write(i, desired & (1 << i));
}

static void send_relay_status() {
  uint8_t d[1] = { g_relay_state };
  bus_tx(CAN_ID_RELAY_STATUS, d, 1);
}

static void send_telemetry() {
#ifdef VBAT_ADC_PIN
  int raw = analogRead(VBAT_ADC_PIN);
  float vadc = (raw / 4095.0f) * 3.3f;
  int16_t vbat_cv = (int16_t)(vadc * VBAT_DIVIDER_RATIO * 100.0f);
  uint8_t d[8] = {};
  pack_i16(&d[0], vbat_cv);
  bus_tx(CAN_ID_TELEMETRY, d, 8);
#endif
}

static void send_cfg_resp(uint8_t idx) {
  if (idx >= NUM_RELAYS) return;
  uint8_t d[8] = { CFG_TARGET_RELAY_CTRL, CFG_KEY_RELAY_MAX_ON_MS, idx, 0, 0, 0, 0, 0 };
  pack_u16(&d[5], g_max_on_ms[idx]);
  bus_tx(CAN_ID_CONFIG_READ_RESP, d, 8);
}

// --------------------------------------------------------------
// NVS config
// --------------------------------------------------------------
static void config_load() {
  g_prefs.begin("relayctl", true);
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    char k[12]; snprintf(k, sizeof(k), "maxon%u", i);
    g_max_on_ms[i] = g_prefs.isKey(k) ? g_prefs.getUShort(k) : RELAY_DEF_MAX[i];
  }
  g_prefs.end();
}

static void config_save() {
  g_prefs.begin("relayctl", false);
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    char k[12]; snprintf(k, sizeof(k), "maxon%u", i);
    g_prefs.putUShort(k, g_max_on_ms[i]);
  }
  g_prefs.end();
  wlogln("[relay] config saved");
}

static void config_reset() {
  g_prefs.begin("relayctl", false); g_prefs.clear(); g_prefs.end();
  for (uint8_t i = 0; i < NUM_RELAYS; i++) g_max_on_ms[i] = RELAY_DEF_MAX[i];
  wlogln("[relay] factory reset");
}

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------
void relay_setup() {
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    relay_write(i, false);
  }
#ifdef VBAT_ADC_PIN
  analogReadResolution(12);
#endif
  config_load();
  wlog("[relay] %u relays ready\n", NUM_RELAYS);
  for (uint8_t i = 0; i < NUM_RELAYS; i++)
    wlog("  relay %u: max_on_ms=%u\n", i, g_max_on_ms[i]);
}

void relay_loop() {
  uint32_t now = millis();

  // Safety watchdog: force off any relay that's been on too long
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    if (!g_max_on_ms[i] || !(g_relay_state & (1 << i))) continue;
    if ((now - g_relay_on_at[i]) >= g_max_on_ms[i]) {
      wlog("[watchdog] relay %u forced OFF after %u ms\n", i, g_max_on_ms[i]);
      relay_write(i, false);
      send_relay_status();
    }
  }

  // Periodic broadcasts
  static uint32_t last_status = 0, last_telem = 0;
  if (now - last_status >= 200)  { last_status = now; send_relay_status(); }
  if (now - last_telem >= 1000)  { last_telem  = now; send_telemetry(); }
}

void relay_handle_frame(const BusFrame& f) {
  auto for_us = [](uint8_t t) {
    return t == CFG_TARGET_RELAY_CTRL || t == CFG_TARGET_BROADCAST;
  };

  switch (f.id) {
    case CAN_ID_RELAY_CMD:
      if (f.dlc >= 2) {
        apply_relay_cmd(f.data[0], f.data[1]);
        wlog("[relay] mask=%02X state=%02X -> relay_state=%02X\n",
             f.data[0], f.data[1], g_relay_state);
        send_relay_status();
      }
      break;

    case CAN_ID_CONFIG_WRITE:
      if (f.dlc < 8 || !for_us(f.data[0])) break;
      {
        uint8_t key = f.data[1], idx = f.data[2];
        uint16_t arg2 = unpack_u16(&f.data[5]);
        if (key == CFG_KEY_RELAY_MAX_ON_MS && idx < NUM_RELAYS) {
          g_max_on_ms[idx] = arg2;
          wlog("[relay cfg] relay %u max_on_ms=%u\n", idx, arg2);
          if (f.data[7] & 0x01) config_save();
          send_cfg_resp(idx);
        }
        if (key == CFG_KEY_WIFI_ENABLED) {
#if USE_WIFI
          bool en = (f.data[4] != 0);
          Preferences p; p.begin("relayctl", false);
          p.putBool("wifi_en", en); p.end();
          wlog("[relay cfg] wifi_en=%u -> restart\n", en);
          delay(100); ESP.restart();
#else
          wlogln("[relay cfg] wifi_en ignored (USE_WIFI=0)");
#endif
        }
      }
      break;

    case CAN_ID_CONFIG_READ_REQ:
      if (f.dlc < 3 || !for_us(f.data[0])) break;
      if (f.data[1] == CFG_KEY_RELAY_MAX_ON_MS) {
        uint8_t idx = f.data[2];
        if (idx == 0xFF) {
          for (uint8_t i = 0; i < NUM_RELAYS; i++) { send_cfg_resp(i); delay(3); }
        } else if (idx < NUM_RELAYS) {
          send_cfg_resp(idx);
        }
      }
      break;

    case CAN_ID_CONFIG_SAVE:
      if (f.dlc < 2 || !for_us(f.data[0])) break;
      switch (f.data[1]) {
        case CFG_SAVE_COMMIT:        config_save(); break;
        case CFG_SAVE_RELOAD:        config_load(); break;
        case CFG_SAVE_FACTORY_RESET: config_reset(); break;
      }
      break;

    default: break;
  }
}

#endif // ENABLE_RELAY
