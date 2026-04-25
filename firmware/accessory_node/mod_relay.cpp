// mod_relay.cpp — relay GPIO control, safety watchdog, NVS config, telemetry,
//                 and relay LCD display (icons, labels, relay widget).

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

// ==============================================================
// Relay LCD display — compiles on any node with ENABLE_LCD.
// Owns relay display data (icons, labels, widget) so mod_lcd
// stays free of relay-specific knowledge.
// ==============================================================
#ifdef ENABLE_LCD

#include "mod_lcd.h"
#include "node_state.h"   // g_relay_mirror

static uint8_t     g_icon_on[LCD_MAX_RELAYS];
static uint8_t     g_icon_off[LCD_MAX_RELAYS];
static const char* g_relay_label[LCD_MAX_RELAYS];
static uint8_t     g_relay_display_count = 6;

uint8_t relay_lcd_count() { return g_relay_display_count; }

char lcd_relay_char(uint8_t idx, bool on) {
  if (idx < LCD_MAX_RELAYS) {
    uint8_t slot = on ? g_icon_on[idx] : g_icon_off[idx];
    if (slot != 0xFF) return (char)slot;
  }
  return on ? (char)0xFF : '-';
}

const char* lcd_relay_label(uint8_t idx) {
  if (idx < LCD_MAX_RELAYS && g_relay_label[idx]) return g_relay_label[idx];
  static char fallback[10];
  snprintf(fallback, sizeof(fallback), "Relay %u", idx + 1);
  return fallback;
}

#ifdef RELAY_WIDGET_ROW
static void relay_render(char* buf, uint8_t width) {
  uint8_t pos = 0;
  if (pos < width) buf[pos++] = '[';
  for (uint8_t i = 0; i < g_relay_display_count && pos + 1 < width; i++)
    buf[pos++] = lcd_relay_char(i, (g_relay_mirror >> i) & 1);
  if (pos < width) buf[pos] = ']';
}
#endif

static void relay_icon_set(uint8_t idx, const char* label, uint8_t slot_on, uint8_t slot_off) {
  if (idx >= LCD_MAX_RELAYS) return;
  g_relay_label[idx] = label;
  g_icon_on[idx]     = slot_on;
  g_icon_off[idx]    = slot_off;
}

#define _ALLOC_ICON(macro) \
  ([]() -> uint8_t { static const uint8_t d[] = macro; return lcd_alloc_cgram(d); }())

void relay_icons_init() {
  memset(g_icon_on,    0xFF, sizeof(g_icon_on));
  memset(g_icon_off,   0xFF, sizeof(g_icon_off));
  memset(g_relay_label, 0,   sizeof(g_relay_label));

#define _RELAY_INIT(N, IDX) \
  { \
    uint8_t on = 0xFF, off = 0xFF; \
    _RELAY_ICON_ON_##N(on)  \
    _RELAY_ICON_OFF_##N(off) \
    relay_icon_set(IDX, _RELAY_LABEL_##N, on, off); \
  }

// Per-relay helper macros — expand to nothing if the config macro isn't defined.
#ifdef RELAY_1_ICON_ON
#define _RELAY_ICON_ON_1(v)  v = _ALLOC_ICON(RELAY_1_ICON_ON);
#else
#define _RELAY_ICON_ON_1(v)
#endif
#ifdef RELAY_1_ICON_OFF
#define _RELAY_ICON_OFF_1(v) v = _ALLOC_ICON(RELAY_1_ICON_OFF);
#else
#define _RELAY_ICON_OFF_1(v)
#endif
#ifdef RELAY_1_LABEL
#define _RELAY_LABEL_1 RELAY_1_LABEL
#else
#define _RELAY_LABEL_1 nullptr
#endif

#ifdef RELAY_2_ICON_ON
#define _RELAY_ICON_ON_2(v)  v = _ALLOC_ICON(RELAY_2_ICON_ON);
#else
#define _RELAY_ICON_ON_2(v)
#endif
#ifdef RELAY_2_ICON_OFF
#define _RELAY_ICON_OFF_2(v) v = _ALLOC_ICON(RELAY_2_ICON_OFF);
#else
#define _RELAY_ICON_OFF_2(v)
#endif
#ifdef RELAY_2_LABEL
#define _RELAY_LABEL_2 RELAY_2_LABEL
#else
#define _RELAY_LABEL_2 nullptr
#endif

#ifdef RELAY_3_ICON_ON
#define _RELAY_ICON_ON_3(v)  v = _ALLOC_ICON(RELAY_3_ICON_ON);
#else
#define _RELAY_ICON_ON_3(v)
#endif
#ifdef RELAY_3_ICON_OFF
#define _RELAY_ICON_OFF_3(v) v = _ALLOC_ICON(RELAY_3_ICON_OFF);
#else
#define _RELAY_ICON_OFF_3(v)
#endif
#ifdef RELAY_3_LABEL
#define _RELAY_LABEL_3 RELAY_3_LABEL
#else
#define _RELAY_LABEL_3 nullptr
#endif

#ifdef RELAY_4_ICON_ON
#define _RELAY_ICON_ON_4(v)  v = _ALLOC_ICON(RELAY_4_ICON_ON);
#else
#define _RELAY_ICON_ON_4(v)
#endif
#ifdef RELAY_4_ICON_OFF
#define _RELAY_ICON_OFF_4(v) v = _ALLOC_ICON(RELAY_4_ICON_OFF);
#else
#define _RELAY_ICON_OFF_4(v)
#endif
#ifdef RELAY_4_LABEL
#define _RELAY_LABEL_4 RELAY_4_LABEL
#else
#define _RELAY_LABEL_4 nullptr
#endif

#ifdef RELAY_5_ICON_ON
#define _RELAY_ICON_ON_5(v)  v = _ALLOC_ICON(RELAY_5_ICON_ON);
#else
#define _RELAY_ICON_ON_5(v)
#endif
#ifdef RELAY_5_ICON_OFF
#define _RELAY_ICON_OFF_5(v) v = _ALLOC_ICON(RELAY_5_ICON_OFF);
#else
#define _RELAY_ICON_OFF_5(v)
#endif
#ifdef RELAY_5_LABEL
#define _RELAY_LABEL_5 RELAY_5_LABEL
#else
#define _RELAY_LABEL_5 nullptr
#endif

#ifdef RELAY_6_ICON_ON
#define _RELAY_ICON_ON_6(v)  v = _ALLOC_ICON(RELAY_6_ICON_ON);
#else
#define _RELAY_ICON_ON_6(v)
#endif
#ifdef RELAY_6_ICON_OFF
#define _RELAY_ICON_OFF_6(v) v = _ALLOC_ICON(RELAY_6_ICON_OFF);
#else
#define _RELAY_ICON_OFF_6(v)
#endif
#ifdef RELAY_6_LABEL
#define _RELAY_LABEL_6 RELAY_6_LABEL
#else
#define _RELAY_LABEL_6 nullptr
#endif

  _RELAY_INIT(1, 0)
  _RELAY_INIT(2, 1)
  _RELAY_INIT(3, 2)
  _RELAY_INIT(4, 3)
  _RELAY_INIT(5, 4)
  _RELAY_INIT(6, 5)
  // Add _RELAY_INIT(7, 6) here and the matching macros above for relay 7

#undef _RELAY_INIT
#undef _ALLOC_ICON

#ifdef RELAY_DISPLAY_COUNT
  g_relay_display_count = RELAY_DISPLAY_COUNT;
#endif

#ifdef RELAY_WIDGET_ROW
#ifndef RELAY_WIDGET_COL
#define RELAY_WIDGET_COL 4
#endif
#ifndef RELAY_WIDGET_WIDTH
#define RELAY_WIDGET_WIDTH 8
#endif
  { LcdWidget w = { RELAY_WIDGET_ROW, RELAY_WIDGET_COL, RELAY_WIDGET_WIDTH, 0, relay_render };
    lcd_register_widget(w); }
#endif
}

#endif // ENABLE_LCD
