// mod_rules.cpp — CAN-frame-triggered rules engine.
//
// Rules live in NVS and survive reboots. Compile-time defaults are
// defined by RULES_DEFAULT_INIT in the node config header. Any slot
// with trig_id == 0 is disabled and skipped during evaluation.
//
// Every frame that comes through bus_rx() is passed to
// rules_handle_frame(). All matching rules fire in index order.
//
// Configured by node_config.h:
//   ENABLE_RULES       — gate the entire module
//   MAX_RULES          — number of rule slots (e.g. 16)
//   RULES_DEFAULT_INIT — brace-enclosed CanRule initialiser list

#include <Arduino.h>
#include <Preferences.h>
#include "node_config.h"

#ifdef ENABLE_RULES

#include "can_protocol.h"
#include "bus.h"
#include "node_state.h"
#include "mod_rules.h"

#ifdef ENABLE_MENU
#include "mod_menu.h"
#endif

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#ifndef RULES_DEFAULT_INIT
#define RULES_DEFAULT_INIT {}
#endif

static const char NVS_NS[] = "rules";

static CanRule  g_rules[MAX_RULES];
static Preferences g_prefs;

// Pending relay timed-off timers — one slot per relay (6 max).
// fire_at_ms == 0 means inactive.
struct RelayTimer { uint8_t relay_mask; uint32_t fire_at_ms; };
static RelayTimer g_timers[6];

static const CanRule DEFAULT_RULES[MAX_RULES] = RULES_DEFAULT_INIT;

// --------------------------------------------------------------
// NVS
// --------------------------------------------------------------
static void rules_load() {
  g_prefs.begin(NVS_NS, true);
  for (uint8_t i = 0; i < MAX_RULES; i++) {
    char k[6]; snprintf(k, sizeof(k), "r%u", i);
    if (g_prefs.getBytesLength(k) == sizeof(CanRule))
      g_prefs.getBytes(k, &g_rules[i], sizeof(CanRule));
    else
      g_rules[i] = DEFAULT_RULES[i];
  }
  g_prefs.end();
}

static void rules_save_all() {
  g_prefs.begin(NVS_NS, false);
  for (uint8_t i = 0; i < MAX_RULES; i++) {
    char k[6]; snprintf(k, sizeof(k), "r%u", i);
    g_prefs.putBytes(k, &g_rules[i], sizeof(CanRule));
  }
  g_prefs.end();
  wlogln("[rules] saved");
}

static void rules_save_one(uint8_t idx) {
  g_prefs.begin(NVS_NS, false);
  char k[6]; snprintf(k, sizeof(k), "r%u", idx);
  g_prefs.putBytes(k, &g_rules[idx], sizeof(CanRule));
  g_prefs.end();
}

// --------------------------------------------------------------
// Evaluation
// --------------------------------------------------------------
static bool rule_matches(const CanRule& r, const BusFrame& f) {
  if (r.trig_id == 0 || r.trig_id != f.id) return false;
  if (r.c0_mask != 0x00) {
    if (f.dlc <= r.c0_byte) return false;
    if ((f.data[r.c0_byte] & r.c0_mask) != (r.c0_val & r.c0_mask)) return false;
  }
  if (r.c1_mask != 0x00) {
    if (f.dlc <= r.c1_byte) return false;
    if ((f.data[r.c1_byte] & r.c1_mask) != (r.c1_val & r.c1_mask)) return false;
  }
  return true;
}

static void rule_execute(const CanRule& r) {
  switch (r.action) {
    case RULE_ACT_RELAY_TOGGLE: {
      uint8_t mask = 1 << r.arg0;
      uint8_t want = (g_relay_mirror & mask) ? 0 : mask;
      uint8_t d[2] = { mask, want };
      bus_tx(CAN_ID_RELAY_CMD, d, 2);
      break;
    }
    case RULE_ACT_RELAY_ON: {
      uint8_t mask = 1 << r.arg0;
      uint8_t d[2] = { mask, mask };
      bus_tx(CAN_ID_RELAY_CMD, d, 2);
      break;
    }
    case RULE_ACT_RELAY_OFF: {
      uint8_t mask = 1 << r.arg0;
      uint8_t d[2] = { mask, 0 };
      bus_tx(CAN_ID_RELAY_CMD, d, 2);
      break;
    }
    case RULE_ACT_ALL_OFF: {
      uint8_t d[2] = { 0x3F, 0x00 };
      bus_tx(CAN_ID_RELAY_CMD, d, 2);
      break;
    }
    case RULE_ACT_RELAY_SCENE: {
      uint8_t d[2] = { 0x3F, r.arg0 & 0x3F };
      bus_tx(CAN_ID_RELAY_CMD, d, 2);
      break;
    }
    case RULE_ACT_LED_ON: {
      uint8_t mask = 1 << r.arg1;
      uint8_t d[3] = { r.arg0, mask, mask };
      bus_tx(CAN_ID_LED_CMD, d, 3);
      break;
    }
    case RULE_ACT_LED_OFF: {
      uint8_t mask = 1 << r.arg1;
      uint8_t d[3] = { r.arg0, mask, 0 };
      bus_tx(CAN_ID_LED_CMD, d, 3);
      break;
    }
    case RULE_ACT_WIFI_ENABLE: {
      uint8_t d[8] = { r.arg0, CFG_KEY_WIFI_ENABLED, 0, 0, 1, 0, 0, 0 };
      bus_tx(CAN_ID_CONFIG_WRITE, d, 8);
      break;
    }
    case RULE_ACT_WIFI_DISABLE: {
      uint8_t d[8] = { r.arg0, CFG_KEY_WIFI_ENABLED, 0, 0, 0, 0, 0, 0 };
      bus_tx(CAN_ID_CONFIG_WRITE, d, 8);
      break;
    }
    case RULE_ACT_VIPER_CMD: {
      uint8_t d[1] = { r.arg0 };
      bus_tx(CAN_ID_VIPER_CMD, d, 1);
      break;
    }
#ifdef ENABLE_MENU
    case RULE_ACT_MENU_SELECT:
      if (menu_is_active()) menu_select();
      break;
    case RULE_ACT_MENU_ENTER:
      if (menu_is_active()) menu_action(); else menu_enter();
      break;
#endif
    case RULE_ACT_RELAY_TIMED_OFF: {
      if (r.arg0 >= 6 || r.arg1 == 0) break;
      uint8_t mask = 1 << r.arg0;
      uint32_t fire_at = millis() + (uint32_t)r.arg1 * 1000UL;
      // Reuse existing slot for this relay or take the first free one.
      int8_t slot = -1;
      for (uint8_t i = 0; i < 6; i++) {
        if (g_timers[i].relay_mask == mask)              { slot = i; break; }
        if (g_timers[i].relay_mask == 0 && slot < 0)    slot = (int8_t)i;
      }
      if (slot >= 0) { g_timers[slot] = { mask, fire_at }; }
      break;
    }
    default: break;
  }
}

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------
void rules_setup() {
  rules_load();
  uint8_t active = 0;
  for (uint8_t i = 0; i < MAX_RULES; i++)
    if (g_rules[i].trig_id != 0) active++;
  wlog("[rules] %u/%u slots active\n", active, MAX_RULES);
}

void rules_tick() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < 6; i++) {
    if (!g_timers[i].relay_mask) continue;
    if (now >= g_timers[i].fire_at_ms) {
      uint8_t d[2] = { g_timers[i].relay_mask, 0 };
      bus_tx(CAN_ID_RELAY_CMD, d, 2);
      g_timers[i].relay_mask = 0;
    }
  }
}

void rules_handle_frame(const BusFrame& f) {
  for (uint8_t i = 0; i < MAX_RULES; i++) {
    if (rule_matches(g_rules[i], f))
      rule_execute(g_rules[i]);
  }
}

uint8_t rules_max() { return MAX_RULES; }

CanRule rules_get(uint8_t idx) {
  if (idx >= MAX_RULES) { CanRule empty{}; return empty; }
  return g_rules[idx];
}

void rules_set(uint8_t idx, const CanRule& r) {
  if (idx >= MAX_RULES) return;
  g_rules[idx] = r;
  rules_save_one(idx);
  wlog("[rules] slot %u updated\n", idx);
}

void rules_clear(uint8_t idx) {
  if (idx >= MAX_RULES) return;
  g_rules[idx] = {};
  rules_save_one(idx);
  wlog("[rules] slot %u cleared\n", idx);
}

void rules_reset_factory() {
  g_prefs.begin(NVS_NS, false); g_prefs.clear(); g_prefs.end();
  for (uint8_t i = 0; i < MAX_RULES; i++) g_rules[i] = DEFAULT_RULES[i];
  rules_save_all();
  wlogln("[rules] factory reset");
}

#endif // ENABLE_RULES
