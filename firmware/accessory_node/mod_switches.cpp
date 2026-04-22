// mod_switches.cpp — switch/button inputs, debounce, action dispatch,
//                    rotary encoder, LCD menu system.
//
// Configured by node_config.h:
//   NUM_SWITCHES, NUM_BUTTONS, INPUT_PINS_INIT
//   ENC_CLK_PIN, ENC_DT_PIN, MENU_BTN_IDX
//   SW_DEFAULT_MAP_INIT
//
// Menu: Long-press MENU_BTN_IDX → enter. Rotate to scroll. Short-press to
// navigate. Long-press to execute. 15 s idle → auto-exit.

#include <Arduino.h>
#include <Preferences.h>
#include "driver/twai.h"
#include "node_config.h"

#ifdef ENABLE_SWITCHES

#include "can_protocol.h"
#include "bus.h"
#include "node_state.h"
#include "mod_lcd.h"

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
  bool    active;
  uint8_t relay_idx;
  uint32_t off_at_ms;
};

static SwitchState  g_sw[NUM_INPUTS];
static SwitchAction g_map[NUM_INPUTS];
static PulseTask    g_pulses[NUM_INPUTS];
static Preferences  g_prefs;

// Encoder
struct EncoderState { uint8_t last_ab; int8_t pending; };
static EncoderState g_enc;

// Menu
#define MENU_TIMEOUT_MS 15000

enum MenuTop : uint8_t {
  MTOP_EXIT = 0, MTOP_RELAYS, MTOP_VIPER, MTOP_BUS, MTOP_DISPLAY, MTOP_WIFI,
  MTOP_COUNT
};
static const char* const MTOP_LABELS[MTOP_COUNT] = {
  "< Exit", "Relays", "Viper", "Bus Status", "Display", "WiFi"
};
// Sub-item counts include leading "< Back" at index 0.
static const uint8_t MTOP_SUB_COUNT[MTOP_COUNT] = { 0, 7, 4, 1, 2, 4 };

static const char* const VIPER_MENU_LABELS[] = { "Lock / Arm", "Unlock/Disarm", "Remote Start" };
static const uint8_t     VIPER_MENU_CMDS[]   = { VIPER_CMD_LOCK, VIPER_CMD_UNLOCK, VIPER_CMD_REMOTE_START };

// Node WiFi state mirror [0=SwitchPanel, 1=RelayCtrl, 2=ViperIface]
static bool g_node_wifi[3] = { true, false, true };

static uint8_t  g_menu_level    = 0;
static uint8_t  g_menu_top_sel  = 0;
static uint8_t  g_menu_sub_sel  = 0;
static uint32_t g_menu_last_act = 0;

// --------------------------------------------------------------
// NVS
// --------------------------------------------------------------
static void config_load() {
  g_prefs.begin("swpanel", true);
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
  g_prefs.begin("swpanel", false);
  for (uint8_t i = 0; i < NUM_INPUTS; i++) {
    char k[8]; snprintf(k, sizeof(k), "sw%u", i);
    g_prefs.putBytes(k, &g_map[i], sizeof(SwitchAction));
  }
  g_prefs.end();
  wlogln("[sw cfg] saved");
}

static void config_reset() {
  g_prefs.begin("swpanel", false); g_prefs.clear(); g_prefs.end();
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

// --------------------------------------------------------------
// Menu system (forward declarations resolved below)
// --------------------------------------------------------------
static void menu_enter();
static void menu_back();
static void menu_select();
static void menu_action();
static void menu_scroll(int8_t dir);

static void handle_press(uint8_t sw_id) {
  if (g_menu_active && sw_id == MENU_BTN_IDX) { menu_select(); return; }
  const SwitchAction& a = g_map[sw_id];
  switch (a.kind) {
    case SW_ACT_TOGGLE:     do_toggle(a.arg); break;
    case SW_ACT_PULSE:      do_pulse(sw_id, a.arg, a.arg2); break;
    case SW_ACT_EVENT_ONLY: break;
    case SW_ACT_HOLD:       do_hold_on(a.arg); break;
    case SW_ACT_SCENE:      do_scene(a.arg); break;
  }
}

static void handle_release(uint8_t sw_id) {
  if (g_map[sw_id].kind == SW_ACT_HOLD) do_hold_off(g_map[sw_id].arg);
}

static void handle_long_press(uint8_t sw_id) {
  if (sw_id == MENU_BTN_IDX) {
    if (g_menu_active) menu_action(); else menu_enter();
    return;
  }
  // SW1 long-press = all off shortcut
  if (sw_id == 0) {
    send_relay_cmd(0x3F, 0x00);
    g_relay_mirror &= ~0x3F;
    wlogln("[sw0 long] ALL OFF");
    lcd_set_event("All OFF");
    lcd_update_status();
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
    if (g_menu_active) { for (uint8_t i = 0; i < n; i++) menu_scroll(1); }
    else send_encoder_event(ENC_ROTATE_CW, n);
  } else if (g_enc.pending <= -4) {
    uint8_t n = (uint8_t)((-g_enc.pending) / 4); g_enc.pending = 0;
    if (g_menu_active) { for (uint8_t i = 0; i < n; i++) menu_scroll(-1); }
    else send_encoder_event(ENC_ROTATE_CCW, n);
  }
}

// --------------------------------------------------------------
// Menu system
// --------------------------------------------------------------
static void menu_draw() {
  char r0[LCD_COLS + 1], r1[LCD_COLS + 1];
  if (g_menu_level == 0) {
    snprintf(r0, sizeof(r0), "MENU  (%u/%u)", g_menu_top_sel + 1, (uint8_t)MTOP_COUNT);
    snprintf(r1, sizeof(r1), "> %s", MTOP_LABELS[g_menu_top_sel]);
  } else {
    switch (g_menu_top_sel) {
      case MTOP_RELAYS:
        if (g_menu_sub_sel == 0) {
          snprintf(r0, sizeof(r0), "Relays");
          snprintf(r1, sizeof(r1), "< Back");
        } else {
          uint8_t idx = g_menu_sub_sel - 1;
          bool on = (g_relay_mirror & (1 << idx)) != 0;
          snprintf(r0, sizeof(r0), "Relays (%u/6)", g_menu_sub_sel);
          snprintf(r1, sizeof(r1), "> Relay %u  [%s]", g_menu_sub_sel, on ? "ON " : "OFF");
        }
        break;
      case MTOP_VIPER:
        if (g_menu_sub_sel == 0) {
          snprintf(r0, sizeof(r0), "Viper");
          snprintf(r1, sizeof(r1), "< Back");
        } else {
          snprintf(r0, sizeof(r0), "Viper  (%u/3)", g_menu_sub_sel);
          snprintf(r1, sizeof(r1), "> %s", VIPER_MENU_LABELS[g_menu_sub_sel - 1]);
        }
        break;
      case MTOP_BUS: {
        twai_status_info_t info;
        if (twai_get_status_info(&info) == ESP_OK) {
          const char* st = (info.state == TWAI_STATE_RUNNING)    ? "OK " :
                           (info.state == TWAI_STATE_BUS_OFF)    ? "OFF" :
                           (info.state == TWAI_STATE_RECOVERING) ? "RCV" : "STP";
          snprintf(r0, sizeof(r0), "%s TX:%u RX:%u", st,
                   info.tx_error_counter, info.rx_error_counter);
        } else {
          snprintf(r0, sizeof(r0), "Bus Status");
        }
        snprintf(r1, sizeof(r1), "< Back");
        break;
      }
      case MTOP_DISPLAY:
        snprintf(r0, sizeof(r0), "Display");
        snprintf(r1, sizeof(r1), g_menu_sub_sel == 0 ? "< Back"
                                  : "> Backlight [%s]", lcd_get_backlight() ? "ON " : "OFF");
        break;
      case MTOP_WIFI: {
        static const char* const WIFI_NODE_NAMES[] = { "SwitchPnl", "RelayCtr ", "Viper    " };
        if (g_menu_sub_sel == 0) {
          snprintf(r0, sizeof(r0), "WiFi");
          snprintf(r1, sizeof(r1), "< Back");
        } else {
          uint8_t ni = g_menu_sub_sel - 1;
          snprintf(r0, sizeof(r0), "WiFi  (%u/3)", g_menu_sub_sel);
          snprintf(r1, sizeof(r1), "> %-9s[%s]", WIFI_NODE_NAMES[ni],
                   g_node_wifi[ni] ? "ON " : "OFF");
        }
        break;
      }
      default:
        snprintf(r0, sizeof(r0), "Menu"); snprintf(r1, sizeof(r1), "---");
        break;
    }
  }
  lcd_write_row(0, r0);
  lcd_write_row(1, r1);
}

static void menu_exit() {
  g_menu_active = false;
  lcd_update_status();
  lcd_set_event("Menu closed");
  wlogln("[menu] exit");
}

static void menu_enter() {
  g_menu_active   = true;
  g_menu_level    = 0;
  g_menu_top_sel  = 0;
  g_menu_sub_sel  = 0;
  g_menu_last_act = millis();
  menu_draw();
  wlogln("[menu] enter");
}

static void menu_back() {
  g_menu_last_act = millis();
  if (g_menu_level == 0) { menu_exit(); return; }
  g_menu_level = 0; g_menu_sub_sel = 0;
  menu_draw();
  wlogln("[menu] back");
}

static void menu_scroll(int8_t dir) {
  g_menu_last_act = millis();
  if (g_menu_level == 0) {
    if (dir > 0) g_menu_top_sel = (g_menu_top_sel + 1) % MTOP_COUNT;
    else         g_menu_top_sel = (g_menu_top_sel + MTOP_COUNT - 1) % MTOP_COUNT;
  } else {
    uint8_t n = MTOP_SUB_COUNT[g_menu_top_sel];
    if (n == 0) { menu_back(); return; }
    if (dir > 0) g_menu_sub_sel = (g_menu_sub_sel + 1) % n;
    else         g_menu_sub_sel = (g_menu_sub_sel + n - 1) % n;
  }
  menu_draw();
}

static void menu_select() {
  g_menu_last_act = millis();
  if (g_menu_level == 0) {
    if (g_menu_top_sel == MTOP_EXIT) { menu_exit(); return; }
    g_menu_level = 1; g_menu_sub_sel = 0;
    menu_draw();
  } else {
    if (g_menu_sub_sel == 0) menu_back();
    // Action items require long-press; short-press is navigation-only.
  }
}

static void menu_action() {
  g_menu_last_act = millis();
  if (g_menu_level == 0) return;
  if (g_menu_sub_sel == 0) { menu_back(); return; }
  uint8_t idx = g_menu_sub_sel - 1;
  switch (g_menu_top_sel) {
    case MTOP_RELAYS:
      do_toggle(idx);
      menu_draw();
      break;
    case MTOP_VIPER: {
      uint8_t d[1] = { VIPER_MENU_CMDS[idx] };
      bus_tx(CAN_ID_VIPER_CMD, d, 1);
      wlog("[menu] viper cmd 0x%02X\n", VIPER_MENU_CMDS[idx]);
      menu_draw();
      break;
    }
    case MTOP_BUS:
      menu_back();
      break;
    case MTOP_DISPLAY:
      lcd_set_backlight(!lcd_get_backlight());
      menu_draw();
      break;
    case MTOP_WIFI: {
      uint8_t ni = idx;  // 0=SwitchPanel(self), 1=RelayCtrl, 2=Viper
      if (ni == 0) {
        bool new_en = !g_node_wifi[0];
        g_prefs.begin("swpanel", false);
        g_prefs.putBool("wifi_en", new_en);
        g_prefs.end();
        wlog("[menu] wifi self -> %u, restart\n", new_en);
        delay(100); ESP.restart();
      } else {
        static const uint8_t WIFI_TARGETS[] = {
          CFG_TARGET_SWITCH_PANEL, CFG_TARGET_RELAY_CTRL, CFG_TARGET_VIPER
        };
        bool new_en = !g_node_wifi[ni];
        uint8_t d[8] = { WIFI_TARGETS[ni], CFG_KEY_WIFI_ENABLED, 0, 0,
                         new_en ? 1u : 0u, 0, 0, 0x01 };
        bus_tx(CAN_ID_CONFIG_WRITE, d, 8);
        g_node_wifi[ni] = new_en;
        wlog("[menu] wifi node%u -> %u\n", ni, new_en);
        menu_draw();
      }
      break;
    }
  }
}

static void menu_tick() {
  if (g_menu_active && (millis() - g_menu_last_act) >= MENU_TIMEOUT_MS)
    menu_exit();
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
    g_sw[i].raw    = digitalRead(INPUT_PINS[i]);
    g_sw[i].stable = g_sw[i].raw;
    g_sw[i].last_change = g_sw[i].press_start = 0;
    g_sw[i].long_sent = false;
    g_pulses[i] = { false, 0, 0 };
  }
  config_load();

  // Seed WiFi mirror so the menu shows correct state on first open
  Preferences p; p.begin("swpanel", true);
  g_node_wifi[0] = p.getBool("wifi_en", true);
  p.end();

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
  menu_tick();
}

void switches_handle_frame(const BusFrame& f) {
  switch (f.id) {
    case CAN_ID_CONFIG_WRITE:
      if (f.dlc < 8 || !cfg_for_us(f.data[0])) break;
      {
        uint8_t key = f.data[1], idx = f.data[2], kind = f.data[3], arg = f.data[4];
        uint16_t arg2 = unpack_u16(&f.data[5]);
        uint8_t flags = f.data[7];
        if (key == CFG_KEY_SW_ACTION && idx < NUM_INPUTS && kind <= SW_ACT_SCENE) {
          g_map[idx] = { kind, arg, arg2 };
          wlog("[sw cfg] sw%u = kind=%u arg=%u arg2=%u\n", idx, kind, arg, arg2);
          if (flags & 0x01) config_save();
          send_cfg_resp(idx);
        }
        if (key == CFG_KEY_WIFI_ENABLED) {
          bool en = (arg != 0);
          g_prefs.begin("swpanel", false);
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
