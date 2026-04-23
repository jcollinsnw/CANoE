// mod_switches.cpp — switch/button inputs, debounce, action dispatch,
//                    rotary encoder. Purely generic: what each input does
//                    is determined entirely by the SwitchAction in g_map[].
//
// Configured by node_config.h:
//   NUM_SWITCHES, NUM_BUTTONS, INPUT_PINS_INIT
//   ENC_CLK_PIN, ENC_DT_PIN
//   SW_DEFAULT_MAP_INIT
//
// Special action kinds (assign in SW_DEFAULT_MAP_INIT or via CONFIG_WRITE):
//   SW_ACT_MENU_NAV — short press navigates menu; long press enters/confirms.
//   SW_ACT_ALL_OFF  — press turns all relays off.

#include <Arduino.h>
#include <Preferences.h>
#include "driver/twai.h"
#include "node_config.h"

#ifdef ENABLE_SWITCHES

#include "can_protocol.h"
#include "bus.h"
#include "node_state.h"
#include "mod_lcd.h"
#include "mod_menu.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#define NUM_INPUTS (NUM_SWITCHES + NUM_BUTTONS)

// --------------------------------------------------------------
// Config-derived tables
// --------------------------------------------------------------
static const uint8_t INPUT_PINS[NUM_INPUTS] = INPUT_PINS_INIT;
static const SwitchAction DEFAULT_MAP[NUM_INPUTS] = SW_DEFAULT_MAP_INIT;

static const uint16_t DEBOUNCE_MS   = 30;
static const uint16_t LONG_PRESS_MS = 600;

// --------------------------------------------------------------
// State
// --------------------------------------------------------------
struct SwitchState {
  uint8_t  raw, stable;
  uint32_t last_change, press_start;
  bool     long_sent;
};

struct PulseTask {
  bool     active;
  uint8_t  relay_idx;
  uint32_t off_at_ms;
};

static SwitchState  g_sw[NUM_INPUTS];
static SwitchAction g_map[NUM_INPUTS];
static PulseTask    g_pulses[NUM_INPUTS];
static Preferences  g_prefs;

// Encoder
struct EncoderState { uint8_t last_ab; int8_t pending; };
static EncoderState g_enc;

// --------------------------------------------------------------
// NVS
// --------------------------------------------------------------
static void config_load() {
  g_prefs.begin(NVS_NAMESPACE, true);
  for (uint8_t i = 0; i < NUM_INPUTS; i++) {
    char k[8]; snprintf(k, sizeof(k), "sw%u", i);
    if (g_prefs.getBytesLength(k) == sizeof(SwitchAction))
      g_prefs.getBytes(k, &g_map[i], sizeof(SwitchAction));
    else
      g_map[i] = DEFAULT_MAP[i];
  }
  g_prefs.end();
}

static void config_save() {
  g_prefs.begin(NVS_NAMESPACE, false);
  for (uint8_t i = 0; i < NUM_INPUTS; i++) {
    char k[8]; snprintf(k, sizeof(k), "sw%u", i);
    g_prefs.putBytes(k, &g_map[i], sizeof(SwitchAction));
  }
  g_prefs.end();
  wlogln("[sw cfg] saved");
}

static void config_reset() {
  g_prefs.begin(NVS_NAMESPACE, false); g_prefs.clear(); g_prefs.end();
  for (uint8_t i = 0; i < NUM_INPUTS; i++) g_map[i] = DEFAULT_MAP[i];
  wlogln("[sw cfg] factory reset");
}

static void send_cfg_resp(uint8_t idx) {
  if (idx >= NUM_INPUTS) return;
  uint8_t d[8] = { CFG_TARGET_SWITCH_PANEL, CFG_KEY_SW_ACTION, idx,
                   g_map[idx].kind, g_map[idx].arg, 0, 0, 0 };
  pack_u16(&d[5], g_map[idx].arg2);
  bus_tx(CAN_ID_CONFIG_READ_RESP, d, 8);
}

// --------------------------------------------------------------
// Outgoing bus frames
// --------------------------------------------------------------
static void send_sw_event(uint8_t id, uint8_t ev) {
  uint8_t d[2] = { id, ev };
  bus_tx(CAN_ID_SWITCH_EVENT, d, 2);
  wlog("[sw%u] event=%u\n", id, ev);
}

static void send_relay_cmd(uint8_t mask, uint8_t desired) {
  uint8_t d[2] = { mask, desired };
  bus_tx(CAN_ID_RELAY_CMD, d, 2);
}

// --------------------------------------------------------------
// Action dispatch helpers
// --------------------------------------------------------------
static void do_toggle(uint8_t relay_idx) {
  if (relay_idx >= 8) return;
  uint8_t mask = 1 << relay_idx;
  uint8_t want = (g_relay_mirror & mask) ? 0 : mask;
  send_relay_cmd(mask, want);
  g_relay_mirror = (g_relay_mirror & ~mask) | want;
  char msg[LCD_COLS + 1];
  snprintf(msg, sizeof(msg), "Relay %u %s", relay_idx + 1, want ? "ON" : "OFF");
  lcd_set_event(msg);
  lcd_update_status();
}

static void do_pulse(uint8_t sw_id, uint8_t relay_idx, uint16_t ms) {
  if (relay_idx >= 8) return;
  uint8_t mask = 1 << relay_idx;
  send_relay_cmd(mask, mask);
  g_relay_mirror |= mask;
  g_pulses[sw_id] = { true, relay_idx, millis() + ms };
  char msg[LCD_COLS + 1];
  snprintf(msg, sizeof(msg), "Relay %u Pulse", relay_idx + 1);
  lcd_set_event(msg);
  lcd_update_status();
}

static void do_hold_on(uint8_t relay_idx) {
  if (relay_idx >= 8) return;
  uint8_t mask = 1 << relay_idx;
  send_relay_cmd(mask, mask);
  g_relay_mirror |= mask;
  char msg[LCD_COLS + 1];
  snprintf(msg, sizeof(msg), "Relay %u ON", relay_idx + 1);
  lcd_set_event(msg);
  lcd_update_status();
}

static void do_hold_off(uint8_t relay_idx) {
  if (relay_idx >= 8) return;
  uint8_t mask = 1 << relay_idx;
  send_relay_cmd(mask, 0);
  g_relay_mirror &= ~mask;
  char msg[LCD_COLS + 1];
  snprintf(msg, sizeof(msg), "Relay %u OFF", relay_idx + 1);
  lcd_set_event(msg);
  lcd_update_status();
}

static void do_scene(uint8_t bitmap) {
  uint8_t state = bitmap & 0x3F;
  send_relay_cmd(0x3F, state);
  g_relay_mirror = (g_relay_mirror & ~0x3F) | state;
  if (state == 0) {
    lcd_set_event("All OFF");
  } else {
    char relays[7];
    for (uint8_t i = 0; i < 6; i++)
      relays[i] = (state & (1 << i)) ? ('1' + i) : '-';
    relays[6] = '\0';
    char msg[LCD_COLS + 1];
    snprintf(msg, sizeof(msg), "Scene [%s]", relays);
    lcd_set_event(msg);
  }
  lcd_update_status();
}

static void do_all_off() {
  send_relay_cmd(0x3F, 0x00);
  g_relay_mirror &= ~0x3F;
  wlogln("[sw] ALL OFF");
  lcd_set_event("All OFF");
  lcd_update_status();
}

// --------------------------------------------------------------
// Press / release / long-press handlers — dispatch on action kind
// --------------------------------------------------------------
static void handle_press(uint8_t sw_id) {
  const SwitchAction& a = g_map[sw_id];
  if (a.kind == SW_ACT_MENU_NAV) {
    if (menu_is_active()) menu_select();
    return;  // short press does nothing when menu is closed
  }
  switch (a.kind) {
    case SW_ACT_TOGGLE:     do_toggle(a.arg); break;
    case SW_ACT_PULSE:      do_pulse(sw_id, a.arg, a.arg2); break;
    case SW_ACT_EVENT_ONLY: break;
    case SW_ACT_HOLD:       do_hold_on(a.arg); break;
    case SW_ACT_SCENE:      do_scene(a.arg); break;
    case SW_ACT_ALL_OFF:    do_all_off(); break;
    default:                break;
  }
}

static void handle_release(uint8_t sw_id) {
  if (g_map[sw_id].kind == SW_ACT_HOLD) do_hold_off(g_map[sw_id].arg);
}

static void handle_long_press(uint8_t sw_id) {
  const SwitchAction& a = g_map[sw_id];
  if (a.kind == SW_ACT_MENU_NAV) {
    if (menu_is_active()) menu_action(); else menu_enter();
  }
}

// --------------------------------------------------------------
// Switch polling
// --------------------------------------------------------------
static void poll_switches() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_INPUTS; i++) {
    SwitchState& s = g_sw[i];
    uint8_t raw = digitalRead(INPUT_PINS[i]);
    if (raw != s.raw) { s.raw = raw; s.last_change = now; }
    if ((now - s.last_change) >= DEBOUNCE_MS && raw != s.stable) {
      s.stable = raw;
      if (s.stable == LOW) {
        s.press_start = now; s.long_sent = false;
        send_sw_event(i, SW_PRESS);
        handle_press(i);
      } else {
        send_sw_event(i, SW_RELEASE);
        handle_release(i);
      }
    }
    if (s.stable == LOW && !s.long_sent && (now - s.press_start) >= LONG_PRESS_MS) {
      s.long_sent = true;
      send_sw_event(i, SW_LONG_PRESS);
      handle_long_press(i);
    }
  }
}

static void service_pulses() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_INPUTS; i++) {
    if (!g_pulses[i].active || (int32_t)(now - g_pulses[i].off_at_ms) < 0) continue;
    uint8_t relay_idx = g_pulses[i].relay_idx;
    uint8_t mask = 1 << relay_idx;
    send_relay_cmd(mask, 0);
    g_relay_mirror &= ~mask;
    g_pulses[i].active = false;
    char msg[LCD_COLS + 1];
    snprintf(msg, sizeof(msg), "Relay %u OFF", relay_idx + 1);
    lcd_set_event(msg);
    lcd_update_status();
  }
}

static void service_hold_safety() {
  for (uint8_t i = 0; i < NUM_INPUTS; i++) {
    const SwitchAction& a = g_map[i];
    if (a.kind != SW_ACT_HOLD) continue;
    if (g_sw[i].stable != LOW) {
      uint8_t mask = 1 << a.arg;
      if (g_relay_mirror & mask) {
        send_relay_cmd(mask, 0);
        g_relay_mirror &= ~mask;
      }
    }
  }
}

// --------------------------------------------------------------
// Rotary encoder (gray-code, CJMCU-111 EC11)
// ±4 sub-steps per physical detent
// --------------------------------------------------------------
static void send_encoder_event(uint8_t ev, uint8_t count = 1) {
  uint8_t d[2] = { ev, count };
  bus_tx(CAN_ID_ENCODER_EVENT, d, 2);
  wlog("[enc] ev=%u count=%u\n", ev, count);
}

static void poll_encoder() {
  static const int8_t STEP[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0,
  };
  uint8_t ab = (digitalRead(ENC_CLK_PIN) << 1) | digitalRead(ENC_DT_PIN);
  g_enc.pending += STEP[(g_enc.last_ab << 2) | ab];
  g_enc.last_ab = ab;

  if (g_enc.pending >= 4) {
    uint8_t n = (uint8_t)(g_enc.pending / 4); g_enc.pending = 0;
    if (menu_is_active()) { for (uint8_t i = 0; i < n; i++) menu_scroll(1); }
    else send_encoder_event(ENC_ROTATE_CW, n);
  } else if (g_enc.pending <= -4) {
    uint8_t n = (uint8_t)((-g_enc.pending) / 4); g_enc.pending = 0;
    if (menu_is_active()) { for (uint8_t i = 0; i < n; i++) menu_scroll(-1); }
    else send_encoder_event(ENC_ROTATE_CCW, n);
  }
}

// --------------------------------------------------------------
// Config frame handler
// --------------------------------------------------------------
static bool cfg_for_us(uint8_t t) {
  return t == CFG_TARGET_SWITCH_PANEL || t == CFG_TARGET_BROADCAST;
}

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------
void switches_setup() {
  for (uint8_t i = 0; i < NUM_INPUTS; i++) {
    pinMode(INPUT_PINS[i], INPUT_PULLUP);
    g_sw[i].raw        = digitalRead(INPUT_PINS[i]);
    g_sw[i].stable     = g_sw[i].raw;
    g_sw[i].last_change = g_sw[i].press_start = 0;
    g_sw[i].long_sent  = false;
    g_pulses[i] = { false, 0, 0 };
  }
  config_load();

  // Encoder — module has onboard pull-ups; no pull-up needed on ESP32 pins
  pinMode(ENC_CLK_PIN, INPUT);
  pinMode(ENC_DT_PIN,  INPUT);
  g_enc.last_ab = (digitalRead(ENC_CLK_PIN) << 1) | digitalRead(ENC_DT_PIN);
  g_enc.pending = 0;

  wlog("[sw] %u inputs ready (%u switches + %u buttons)\n",
       NUM_INPUTS, NUM_SWITCHES, NUM_BUTTONS);
  for (uint8_t i = 0; i < NUM_INPUTS; i++)
    wlog("  %s%u: kind=%u arg=%u arg2=%u\n",
         i < NUM_SWITCHES ? "sw" : "btn",
         i < NUM_SWITCHES ? i + 1 : i - NUM_SWITCHES + 1,
         g_map[i].kind, g_map[i].arg, g_map[i].arg2);
}

void switches_loop() {
  poll_switches();
  service_pulses();
  service_hold_safety();
  poll_encoder();
}

void switches_handle_frame(const BusFrame& f) {
  switch (f.id) {
    case CAN_ID_CONFIG_WRITE:
      if (f.dlc < 8 || !cfg_for_us(f.data[0])) break;
      {
        uint8_t key = f.data[1], idx = f.data[2], kind = f.data[3], arg = f.data[4];
        uint16_t arg2 = unpack_u16(&f.data[5]);
        uint8_t flags = f.data[7];
        if (key == CFG_KEY_SW_ACTION && idx < NUM_INPUTS && kind <= SW_ACT_ALL_OFF) {
          g_map[idx] = { kind, arg, arg2 };
          wlog("[sw cfg] sw%u = kind=%u arg=%u arg2=%u\n", idx, kind, arg, arg2);
          if (flags & 0x01) config_save();
          send_cfg_resp(idx);
        }
        if (key == CFG_KEY_WIFI_ENABLED) {
          bool en = (arg != 0);
          g_prefs.begin(NVS_NAMESPACE, false);
          g_prefs.putBool("wifi_en", en);
          g_prefs.end();
          wlog("[sw cfg] wifi_en=%u -> restart\n", en);
          delay(100); ESP.restart();
        }
      }
      break;

    case CAN_ID_CONFIG_READ_REQ:
      if (f.dlc < 3 || !cfg_for_us(f.data[0])) break;
      if (f.data[1] == CFG_KEY_SW_ACTION) {
        uint8_t idx = f.data[2];
        if (idx == 0xFF) {
          for (uint8_t i = 0; i < NUM_INPUTS; i++) { send_cfg_resp(i); delay(3); }
        } else if (idx < NUM_INPUTS) {
          send_cfg_resp(idx);
        }
      }
      break;

    case CAN_ID_CONFIG_SAVE:
      if (f.dlc < 2 || !cfg_for_us(f.data[0])) break;
      switch (f.data[1]) {
        case CFG_SAVE_COMMIT:        config_save(); break;
        case CFG_SAVE_RELOAD:        config_load(); break;
        case CFG_SAVE_FACTORY_RESET: config_reset(); break;
      }
      break;

    default: break;
  }
}

#endif // ENABLE_SWITCHES
