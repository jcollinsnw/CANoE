// switch_panel.ino
// ESP32 programmable switch panel with WiFi fallback and web UI.
//
// Supported action kinds (configurable at runtime via CAN):
//   SW_ACT_TOGGLE     — press toggles relay `arg`
//   SW_ACT_PULSE      — press turns relay `arg` on for `arg2` ms
//   SW_ACT_EVENT_ONLY — publish event only, no relay change
//   SW_ACT_HOLD       — relay `arg` ON while held, OFF on release (horn)
//   SW_ACT_SCENE      — press sets all 6 relays to bitmap in `arg`
//
// Bus transport: wired TWAI + ESP-NOW simultaneously, via bus.cpp.
// Web UI + captive portal: webui.cpp (shared with the other node).
//
// Inputs: 6 latching switches (SW1–SW6, indices 0–5)
//         4 momentary buttons  (BTN1–BTN4, indices 6–9)

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include "driver/twai.h"
#include "soc/gpio_struct.h"

#include "can_protocol.h"
#include "bus.h"

// ==============================================================
// CONFIG
// ==============================================================
#define USE_CAN_TRANSCEIVER 0
#define USE_WIFI 1           // 0 = CAN-only, no SoftAP, no web UI, no ESP-NOW

#if USE_WIFI
#include "webui.h"
#else
// Fallback: wlog/wlogln just go to Serial when WiFi is off
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#define CAN_TX_PIN GPIO_NUM_5
#define CAN_RX_PIN GPIO_NUM_4

#define NUM_SWITCHES 6   // latching switches (SW1–SW6, indices 0–5)
#define NUM_BUTTONS  4   // momentary buttons  (BTN1–BTN4, indices 6–9)
#define NUM_INPUTS   (NUM_SWITCHES + NUM_BUTTONS)

// First NUM_SWITCHES entries are latching switches; remaining are momentary buttons.
static const uint8_t INPUT_PINS[NUM_INPUTS] = {
  25, 26, 27, 32, 33, 13,  // SW1–SW6
  14, 18, 19, 23,           // BTN1–BTN4 (GPIO 21 freed for I2C SDA)
};

static const SwitchAction DEFAULT_INPUT_MAP[NUM_INPUTS] = {
  {SW_ACT_TOGGLE,     0, 0},     // SW1  -> toggle relay 1
  {SW_ACT_TOGGLE,     1, 0},     // SW2  -> toggle relay 2
  {SW_ACT_TOGGLE,     2, 0},     // SW3  -> toggle relay 3
  {SW_ACT_TOGGLE,     3, 0},     // SW4  -> toggle relay 4
  {SW_ACT_HOLD,       4, 0},     // SW5  -> HOLD relay 5 (horn)
  {SW_ACT_PULSE,      5, 3000},  // SW6  -> pulse relay 6 for 3 s
  {SW_ACT_EVENT_ONLY, 0, 0},     // BTN1 -> event only (configure at runtime)
  {SW_ACT_EVENT_ONLY, 0, 0},     // BTN2 -> event only
  {SW_ACT_EVENT_ONLY, 0, 0},     // BTN3 -> event only
  {SW_ACT_EVENT_ONLY, 0, 0},     // BTN4 -> event only
};

// I2C LCD (HD44780 with PCF8574 backpack, 16×2)
#define LCD_I2C_ADDR  0x27   // PCF8574 address; try 0x3F if display stays blank
#define LCD_COLS      16
#define LCD_ROWS      2
#define LCD_SDA_PIN   21     // default ESP32 SDA (freed by moving BTN4 to GPIO 23)
#define LCD_SCL_PIN   22     // default ESP32 SCL

// Rotary encoder — GA/GB are input-only GPIOs on ESP32.
// Module has onboard 3.3 kΩ pull-ups (marked 332); connect module VCC to 3V3.
// No SW pin on this module — encoder button is a dedicated panel button (see menu system).
#define ENC_CLK_PIN   34     // GA (CLK/A)
#define ENC_DT_PIN    35     // GB (DT/B)
#define MENU_BTN_IDX  6      // BTN1 = input index 6 (GPIO 14) — encoder select/back button

// AM2302 (DHT22) temperature/humidity sensor
// WARNING: GPIO 0 is a strapping pin — it must be HIGH at boot or the
// ESP32 enters download mode. The AM2302's pull-up resistor (typically
// included on breakout boards) keeps the line high at idle, so this is
// usually fine. If the board won't boot, move the sensor to another GPIO.
#define DHT_PIN       0
#define DHT_INTERVAL_MS 5000  // broadcast every 5 s (sensor min is ~2 s)

static const uint16_t DEBOUNCE_MS   = 30;
static const uint16_t LONG_PRESS_MS = 600;

static const char    NODE_NAME[] = "switch-panel";
static const uint8_t NODE_ID     = 0x01;

// ==============================================================
// State
// ==============================================================
struct SwitchState {
  uint8_t raw;
  uint8_t stable;
  uint32_t last_change;
  uint32_t press_start;
  bool long_sent;
};

static SwitchState  g_sw[NUM_INPUTS];
static SwitchAction g_map[NUM_INPUTS];
static uint8_t      g_relay_mirror  = 0;
static bool         g_can_ok        = true;
static bool         g_menu_active   = false;
static bool         g_lcd_backlight = true;

struct PulseTask {
  bool     active;
  uint8_t  relay_idx;
  uint32_t off_at_ms;
};
static PulseTask g_pulses[NUM_INPUTS];

static Preferences g_prefs;

// ==============================================================
// NVS
// ==============================================================
static void config_load() {
  g_prefs.begin("swpanel", true);
  for (uint8_t i = 0; i < NUM_INPUTS; i++) {
    char k[8]; snprintf(k, sizeof(k), "sw%u", i);
    if (g_prefs.getBytesLength(k) == sizeof(SwitchAction))
      g_prefs.getBytes(k, &g_map[i], sizeof(SwitchAction));
    else
      g_map[i] = DEFAULT_INPUT_MAP[i];
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
  wlogln("[cfg] saved");
}
static void config_factory_reset() {
  g_prefs.begin("swpanel", false); g_prefs.clear(); g_prefs.end();
  for (uint8_t i = 0; i < NUM_INPUTS; i++) g_map[i] = DEFAULT_INPUT_MAP[i];
  wlogln("[cfg] factory reset");
}

// ==============================================================
// LCD driver (HD44780 via PCF8574 I2C backpack)
// ==============================================================
#define LCD_BL 0x08  // backlight bit in the PCF8574 byte
#define LCD_EN 0x04  // enable strobe
#define LCD_RS 0x01  // register select: 0 = command, 1 = data

static void lcd_i2c_write(uint8_t v) {
  Wire.beginTransmission(LCD_I2C_ADDR);
  Wire.write(v | (g_lcd_backlight ? LCD_BL : 0));
  Wire.endTransmission();
}
static void lcd_pulse(uint8_t v) {
  lcd_i2c_write(v | LCD_EN); delayMicroseconds(1);
  lcd_i2c_write(v & ~LCD_EN); delayMicroseconds(50);
}
static void lcd_nibble(uint8_t n, bool rs) {
  lcd_pulse((n << 4) | (rs ? LCD_RS : 0));
}
static void lcd_byte(uint8_t b, bool rs) {
  lcd_nibble(b >> 4, rs); lcd_nibble(b & 0x0F, rs); delayMicroseconds(40);
}
static void lcd_cmd(uint8_t c)  { lcd_byte(c, false); }
static void lcd_data(uint8_t c) { lcd_byte(c, true);  }

static void lcd_set_cursor(uint8_t row, uint8_t col) {
  static const uint8_t ROW_OFF[] = { 0x00, 0x40 };
  lcd_cmd(0x80 | (ROW_OFF[row & 1] + (col & 0x0F)));
}
static void lcd_clear() { lcd_cmd(0x01); delay(2); }
static void lcd_print_n(const char* s, uint8_t len) {
  for (uint8_t i = 0; i < len && s[i]; i++) lcd_data((uint8_t)s[i]);
}
// ==============================================================
// LCD auto-display (status row 0, event row 1)
// ==============================================================
static void lcd_write_row(uint8_t row, const char* text) {
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "%-16s", text);
  lcd_set_cursor(row, 0);
  lcd_print_n(buf, LCD_COLS);
}

static void lcd_update_status() {
  if (g_menu_active) return;
  char relays[7];
  for (uint8_t i = 0; i < 6; i++)
    relays[i] = (g_relay_mirror & (1 << i)) ? ('1' + i) : '-';
  relays[6] = '\0';
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "CAN:%-3s [%s]", g_can_ok ? "OK" : "ERR", relays);
  lcd_write_row(0, buf);
}

static void lcd_set_event(const char* msg) {
  if (g_menu_active) return;
  lcd_write_row(1, msg);
}

// ==============================================================
static void lcd_init() {
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
  delay(50);
  // HD44780 4-bit power-on init sequence (per datasheet)
  lcd_nibble(0x03, false); delay(5);
  lcd_nibble(0x03, false); delayMicroseconds(150);
  lcd_nibble(0x03, false);
  lcd_nibble(0x02, false);  // enter 4-bit mode
  lcd_cmd(0x28);            // function: 4-bit, 2 lines, 5×8 dots
  lcd_cmd(0x08);            // display off
  lcd_clear();
  lcd_cmd(0x06);            // entry mode: increment, no display shift
  lcd_cmd(0x0C);            // display on, cursor off, no blink
  wlogln("[LCD] init OK");
}

// ==============================================================
// Outgoing frames
// ==============================================================
static void send_sw_event(uint8_t id, uint8_t ev) {
  uint8_t d[2] = { id, ev };
  bus_tx(CAN_ID_SWITCH_EVENT, d, 2);
  wlog("[sw%u] event=%u\n", id, ev);
}
static void send_relay_cmd(uint8_t mask, uint8_t desired) {
  uint8_t d[2] = { mask, desired };
  bus_tx(CAN_ID_RELAY_CMD, d, 2);
}
static void send_cfg_read_resp(uint8_t idx) {
  if (idx >= NUM_INPUTS) return;
  uint8_t d[8] = {
    CFG_TARGET_SWITCH_PANEL, CFG_KEY_SW_ACTION, idx,
    g_map[idx].kind, g_map[idx].arg, 0, 0, 0
  };
  pack_u16(&d[5], g_map[idx].arg2);
  bus_tx(CAN_ID_CONFIG_READ_RESP, d, 8);
}

// Forward declarations — menu functions are defined after the encoder section
static void menu_enter();
static void menu_back();
static void menu_select();
static void menu_action();
static void menu_scroll(int8_t dir);

// ==============================================================
// Action dispatch
// ==============================================================
static void do_toggle(uint8_t relay_idx) {
  if (relay_idx >= 8) return;
  uint8_t mask = 1 << relay_idx;
  uint8_t cur  = (g_relay_mirror & mask) ? mask : 0;
  uint8_t want = (cur ^ mask) & mask;
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
  const SwitchAction& a = g_map[sw_id];
  if (a.kind == SW_ACT_HOLD) do_hold_off(a.arg);
}
static void handle_long_press(uint8_t sw_id) {
  if (sw_id == MENU_BTN_IDX) {
    if (g_menu_active) menu_action(); else menu_enter();
    return;
  }
  if (sw_id == 0) {
    send_relay_cmd(0x3F, 0x00);
    g_relay_mirror &= ~0x3F;
    wlogln("[sw0 long] ALL OFF");
    lcd_set_event("All OFF");
    lcd_update_status();
  }
}

// ==============================================================
// Config handlers
// ==============================================================
static bool cfg_for_us(uint8_t t) { return t == CFG_TARGET_SWITCH_PANEL || t == CFG_TARGET_BROADCAST; }
static void handle_cfg_write(const BusFrame& f) {
  if (f.dlc < 8 || !cfg_for_us(f.data[0])) return;
  uint8_t key = f.data[1], idx = f.data[2], kind = f.data[3], arg = f.data[4];
  uint16_t arg2 = unpack_u16(&f.data[5]);
  uint8_t flags = f.data[7];
  if (key == CFG_KEY_SW_ACTION && idx < NUM_INPUTS && kind <= SW_ACT_SCENE) {
    g_map[idx] = { kind, arg, arg2 };
    wlog("[cfg<-] sw%u = kind=%u arg=%u arg2=%u\n", idx, kind, arg, arg2);
    if (flags & 0x01) config_save();
    send_cfg_read_resp(idx);
  }
  if (key == CFG_KEY_WIFI_ENABLED) {
    bool en = (arg != 0);
    g_prefs.begin("swpanel", false);
    g_prefs.putBool("wifi_en", en);
    g_prefs.end();
    wlog("[cfg<-] wifi_en=%u -> restart\n", en);
    delay(100);
    ESP.restart();
  }
}
static void handle_cfg_read(const BusFrame& f) {
  if (f.dlc < 3 || !cfg_for_us(f.data[0])) return;
  if (f.data[1] != CFG_KEY_SW_ACTION) return;
  uint8_t idx = f.data[2];
  if (idx == 0xFF) {
    for (uint8_t i = 0; i < NUM_INPUTS; i++) { send_cfg_read_resp(i); delay(3); }
  } else if (idx < NUM_INPUTS) {
    send_cfg_read_resp(idx);
  }
}
static void handle_cfg_save(const BusFrame& f) {
  if (f.dlc < 2 || !cfg_for_us(f.data[0])) return;
  switch (f.data[1]) {
    case CFG_SAVE_COMMIT:        config_save(); break;
    case CFG_SAVE_RELOAD:        config_load(); break;
    case CFG_SAVE_FACTORY_RESET: config_factory_reset(); break;
  }
}

// ==============================================================
// Peripheral command handlers
// ==============================================================
static void handle_lcd_cmd(const BusFrame& f) {
  // Frame layout: [row, col, char0 .. char5]
  //   row = 0xFF  → clear display; remaining bytes ignored.
  //   row = 0/1   → position cursor then write up to 6 characters.
  if (f.dlc < 1) return;
  if (f.data[0] == 0xFF) { lcd_clear(); return; }
  if (f.data[0] >= LCD_ROWS || f.dlc < 2) return;
  lcd_set_cursor(f.data[0], f.data[1]);
  if (f.dlc > 2) lcd_print_n((const char*)&f.data[2], f.dlc - 2);
}

// ==============================================================
// Switch polling
// ==============================================================
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
        send_sw_event(i, SW_PRESS); handle_press(i);
      } else {
        send_sw_event(i, SW_RELEASE); handle_release(i);
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
    if (g_pulses[i].active && (int32_t)(now - g_pulses[i].off_at_ms) >= 0) {
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

// ==============================================================
// Rotary encoder
// ==============================================================
struct EncoderState {
  uint8_t  last_ab;   // previous GA/GB state for gray-code decode
  int8_t   pending;   // accumulated steps (±4 = one detent)
};
static EncoderState g_enc;

static void send_encoder_event(uint8_t ev, uint8_t count = 1) {
  uint8_t d[2] = { ev, count };
  bus_tx(CAN_ID_ENCODER_EVENT, d, 2);
  wlog("[enc] ev=%u count=%u\n", ev, count);
}

static void poll_encoder() {
  // Gray-code state machine — accumulate ±4 steps per detent
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
    uint8_t n = (uint8_t)(g_enc.pending / 4);
    g_enc.pending = 0;
    if (g_menu_active) { for (uint8_t i = 0; i < n; i++) menu_scroll(1); }
    else send_encoder_event(ENC_ROTATE_CW, n);
  } else if (g_enc.pending <= -4) {
    uint8_t n = (uint8_t)((-g_enc.pending) / 4);
    g_enc.pending = 0;
    if (g_menu_active) { for (uint8_t i = 0; i < n; i++) menu_scroll(-1); }
    else send_encoder_event(ENC_ROTATE_CCW, n);
  }
}

// ==============================================================
// AM2302 (DHT22) — bit-bang one-wire protocol, no library needed
// ==============================================================
// CAN_ID_ENV_DATA payload (4 bytes):
//   [0-1] temperature in 0.1 °C, signed int16 LE  (e.g. 234 = 23.4 °C)
//   [2-3] humidity    in 0.1 %RH, uint16 LE       (e.g. 512 = 51.2 %)

static int16_t  g_env_temp_d1 = 0;   // last reading, 0.1 °C
static uint16_t g_env_humi_d1 = 0;   // last reading, 0.1 %RH
static bool     g_env_valid   = false;

// Read 40 bits from the AM2302.  Returns true on success.
static bool dht_read(int16_t &temp_d1, uint16_t &humi_d1) {
  // Host pulls low ≥1 ms to wake sensor
  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, LOW);
  delay(2);
  digitalWrite(DHT_PIN, HIGH);
  delayMicroseconds(30);
  pinMode(DHT_PIN, INPUT_PULLUP);

  // Wait for sensor pull-low (~80 µs) then pull-high (~80 µs)
  uint32_t t0 = micros();
  while (digitalRead(DHT_PIN) == HIGH) { if (micros() - t0 > 200) return false; }
  t0 = micros();
  while (digitalRead(DHT_PIN) == LOW)  { if (micros() - t0 > 200) return false; }
  t0 = micros();
  while (digitalRead(DHT_PIN) == HIGH) { if (micros() - t0 > 200) return false; }

  // Read 40 bits: each bit starts with ~50 µs low, then 26-28 µs high = 0, 70 µs high = 1
  uint8_t data[5] = {};
  for (int i = 0; i < 40; i++) {
    t0 = micros();
    while (digitalRead(DHT_PIN) == LOW)  { if (micros() - t0 > 100) return false; }
    t0 = micros();
    while (digitalRead(DHT_PIN) == HIGH) { if (micros() - t0 > 100) return false; }
    if ((micros() - t0) > 40) data[i / 8] |= (1 << (7 - (i % 8)));
  }

  // Checksum
  uint8_t ck = data[0] + data[1] + data[2] + data[3];
  if (ck != data[4]) return false;

  humi_d1 = ((uint16_t)data[0] << 8) | data[1];
  uint16_t t_raw = ((uint16_t)data[2] << 8) | data[3];
  if (t_raw & 0x8000)
    temp_d1 = -(int16_t)(t_raw & 0x7FFF);
  else
    temp_d1 = (int16_t)t_raw;
  return true;
}

static void send_env_data() {
  uint8_t d[4];
  pack_i16(&d[0], g_env_temp_d1);
  pack_u16(&d[2], g_env_humi_d1);
  bus_tx(CAN_ID_ENV_DATA, d, 4);
}

static void poll_dht() {
  static uint32_t last_read = 0;
  uint32_t now = millis();
  if (now - last_read < DHT_INTERVAL_MS) return;
  last_read = now;

  int16_t t; uint16_t h;
  if (dht_read(t, h)) {
    g_env_temp_d1 = t;
    g_env_humi_d1 = h;
    g_env_valid = true;
    send_env_data();
    wlog("[dht] %.1fC  %.1f%%\n", t / 10.0f, h / 10.0f);
  } else {
    wlogln("[dht] read failed");
  }
}

// ==============================================================
// Menu system
//   Long-press BTN1 from idle  → enter menu
//   Short-press BTN1 in menu   → navigate: enter submenu, or confirm "< Back"/"< Exit"
//   Long-press BTN1 in menu    → execute / toggle the highlighted action item
//   Rotate encoder in menu     → scroll up / down one item per detent
//   15 s of no input           → auto-exit back to status display
//
//   Every list starts with a "< Back" (or "< Exit" at top level) item.
//   Scroll to it and short-press to go up a level.
//   Relay / Viper / Backlight actions require a long-press to execute.
// ==============================================================
#define MENU_TIMEOUT_MS 15000

// Top-level items. MTOP_EXIT is always index 0 so it is the first thing you see
// and can be reached by one CCW scroll from any position (wraps).
enum MenuTop : uint8_t {
  MTOP_EXIT    = 0,   // "< Exit"    — short press exits menu
  MTOP_RELAYS  = 1,
  MTOP_VIPER   = 2,
  MTOP_BUS     = 3,
  MTOP_DISPLAY = 4,
  MTOP_WIFI    = 5,
  MTOP_COUNT   = 6,
};

static const char* const MTOP_LABELS[MTOP_COUNT] = {
  "< Exit", "Relays", "Viper", "Bus Status", "Display", "WiFi"
};
// Sub-item counts include the leading "< Back" entry at index 0.
// MTOP_EXIT has no submenu (0). MTOP_BUS has only "< Back" (1).
static const uint8_t MTOP_SUB_COUNT[MTOP_COUNT] = { 0, 7, 4, 1, 2, 4 };
//  EXIT: —  RELAYS: back+6  VIPER: back+3  BUS: back  DISPLAY: back+1  WIFI: back+3

// Node WiFi state mirror [0=SwitchPanel, 1=RelayCtrl, 2=ViperIface]
// Relay ctrl is compiled with USE_WIFI=0 so its entry is informational only.
static bool g_node_wifi[3] = { true, false, true };

static const char* const VIPER_MENU_LABELS[] = {
  "Lock / Arm", "Unlock/Disarm", "Remote Start"
};
static const uint8_t VIPER_MENU_CMDS[] = {
  VIPER_CMD_LOCK, VIPER_CMD_UNLOCK, VIPER_CMD_REMOTE_START
};

static uint8_t  g_menu_level    = 0;
static uint8_t  g_menu_top_sel  = 0;
static uint8_t  g_menu_sub_sel  = 0;
static uint32_t g_menu_last_act = 0;

static void menu_draw() {
  char r0[LCD_COLS + 1], r1[LCD_COLS + 1];
  if (g_menu_level == 0) {
    snprintf(r0, sizeof(r0), "MENU  (%u/%u)", g_menu_top_sel + 1, (uint8_t)MTOP_COUNT);
    snprintf(r1, sizeof(r1), "> %s", MTOP_LABELS[g_menu_top_sel]);
  } else {
    // Sub-item 0 is always "< Back"; items 1-N are the real actions (0-based idx = sub_sel - 1).
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
        // Only item is "< Back"; show live bus stats on the title row.
        twai_status_info_t info;
        if (twai_get_status_info(&info) == ESP_OK) {
          const char* st = (info.state == TWAI_STATE_RUNNING)    ? "OK " :
                           (info.state == TWAI_STATE_BUS_OFF)    ? "OFF" :
                           (info.state == TWAI_STATE_RECOVERING) ? "RCV" : "STP";
          snprintf(r0, sizeof(r0), "%s TX:%u RX:%u",
                   st, info.tx_error_counter, info.rx_error_counter);
        } else {
          snprintf(r0, sizeof(r0), "Bus Status");
        }
        snprintf(r1, sizeof(r1), "< Back");
        break;
      }
      case MTOP_DISPLAY:
        snprintf(r0, sizeof(r0), "Display");
        if (g_menu_sub_sel == 0)
          snprintf(r1, sizeof(r1), "< Back");
        else
          snprintf(r1, sizeof(r1), "> Backlight [%s]", g_lcd_backlight ? "ON " : "OFF");
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
  g_menu_active = false;   // clear flag before LCD calls so guards pass
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
  if (g_menu_level == 0) {
    menu_exit();
  } else {
    g_menu_level   = 0;
    g_menu_sub_sel = 0;
    menu_draw();
    wlogln("[menu] back");
  }
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

// Short press: navigation only — enter submenu, or confirm "< Back" / "< Exit".
static void menu_select() {
  g_menu_last_act = millis();
  if (g_menu_level == 0) {
    if (g_menu_top_sel == MTOP_EXIT) { menu_exit(); return; }
    g_menu_level   = 1;
    g_menu_sub_sel = 0;   // land on "< Back" so user can leave immediately if needed
    menu_draw();
  } else {
    if (g_menu_sub_sel == 0) menu_back();
    // Action items require a long-press — short press is a no-op here.
  }
}

// Long press: execute / toggle the highlighted action item.
static void menu_action() {
  g_menu_last_act = millis();
  if (g_menu_level == 0) return;            // no actions at top level
  if (g_menu_sub_sel == 0) { menu_back(); return; }  // long press on "< Back" also backs out
  uint8_t idx = g_menu_sub_sel - 1;
  switch (g_menu_top_sel) {
    case MTOP_RELAYS:
      do_toggle(idx);   // lcd calls inside do_toggle suppressed by g_menu_active guard
      menu_draw();
      break;
    case MTOP_VIPER: {
      uint8_t d[1] = { VIPER_MENU_CMDS[idx] };
      bus_tx(CAN_ID_VIPER_CMD, d, 1);
      wlog("[menu] viper cmd %u\n", VIPER_MENU_CMDS[idx]);
      menu_draw();
      break;
    }
    case MTOP_BUS:
      menu_back();
      break;
    case MTOP_DISPLAY:
      g_lcd_backlight = !g_lcd_backlight;
      menu_draw();
      break;
    case MTOP_WIFI: {
      uint8_t ni = idx;   // 0=SwitchPanel(self), 1=RelayCtrl, 2=Viper
      if (ni == 0) {
        // Self: write NVS then restart to apply cleanly
        bool new_en = !g_node_wifi[0];
        g_prefs.begin("swpanel", false);
        g_prefs.putBool("wifi_en", new_en);
        g_prefs.end();
        wlog("[menu] wifi self -> %u, restart\n", new_en);
        delay(100);
        ESP.restart();
      } else {
        // Remote: send CONFIG_WRITE; target node will restart
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

// ==============================================================
// CAN setup
// ==============================================================
static void setup_can() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NO_ACK);
  g.tx_queue_len = 10; g.rx_queue_len = 20;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) {
    wlogln("[CAN] init failed"); while (true) delay(1000);
  }
#if !USE_CAN_TRANSCEIVER
  // Set TX pin to open-drain via register to preserve TWAI signal routing
  GPIO.pin[CAN_TX_PIN].pad_driver = 1;  // 1 = open-drain
  gpio_set_pull_mode(CAN_TX_PIN, GPIO_PULLUP_ONLY);
  wlogln("[CAN] BENCH mode (open-drain TX, NO_ACK)");
#else
  wlogln("[CAN] TRANSCEIVER mode (NO_ACK)");
#endif
}

// ==============================================================
// Arduino lifecycle
// ==============================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  wlogln("");
  wlogln("=== Switch Panel boot ===");

  for (uint8_t i = 0; i < NUM_INPUTS; i++) {
    pinMode(INPUT_PINS[i], INPUT_PULLUP);
    g_sw[i].raw = digitalRead(INPUT_PINS[i]);
    g_sw[i].stable = g_sw[i].raw;
    g_sw[i].last_change = g_sw[i].press_start = 0;
    g_sw[i].long_sent = false;
    g_pulses[i] = { false, 0, 0 };
  }

  config_load();
  // Seed our own wifi mirror from NVS so the WiFi menu shows the correct state
  {
    Preferences p; p.begin("swpanel", true);
    g_node_wifi[0] = p.getBool("wifi_en", true);
    p.end();
  }

  setup_can();
#if USE_WIFI
  if (g_node_wifi[0]) { webui_init(NODE_NAME, NODE_ID); bus_init(NODE_ID); }
  else                 { wlogln("[boot] WiFi disabled by NVS flag"); bus_init_no_wifi(NODE_ID); }
#else
  bus_init_no_wifi(NODE_ID);
#endif

  lcd_init();

  // Encoder GA/GB — input-only GPIOs; module VCC to 3V3 provides onboard pull-ups
  pinMode(ENC_CLK_PIN, INPUT);
  pinMode(ENC_DT_PIN,  INPUT);
  g_enc.last_ab = (digitalRead(ENC_CLK_PIN) << 1) | digitalRead(ENC_DT_PIN);
  g_enc.pending = 0;

  lcd_update_status();
  lcd_set_event("Switch Panel");

  wlog("[boot] %u inputs ready (%u switches + %u buttons)\n", NUM_INPUTS, NUM_SWITCHES, NUM_BUTTONS);
  for (uint8_t i = 0; i < NUM_INPUTS; i++)
    wlog("  %s%u: kind=%u arg=%u arg2=%u\n",
                  i < NUM_SWITCHES ? "sw" : "btn", i < NUM_SWITCHES ? i + 1 : i - NUM_SWITCHES + 1,
                  g_map[i].kind, g_map[i].arg, g_map[i].arg2);
}

void loop() {
#if USE_WIFI
  webui_tick();
#endif
  bus_tick();
  poll_switches();
  service_pulses();
  service_hold_safety();
  poll_encoder();
  poll_dht();
  menu_tick();

  BusFrame f;
  while (bus_rx(f)) {
    switch (f.id) {
      case CAN_ID_RELAY_CMD:
        if (f.dlc >= 2) {
          uint8_t mask = f.data[0], state = f.data[1];
          g_relay_mirror = (g_relay_mirror & ~mask) | (state & mask);
          char msg[LCD_COLS + 1];
          if (__builtin_popcount(mask) == 1) {
            uint8_t idx = __builtin_ctz(mask);
            snprintf(msg, sizeof(msg), "Relay %u %s", idx + 1, (state & mask) ? "ON" : "OFF");
          } else if (mask == 0x3F && state == 0) {
            snprintf(msg, sizeof(msg), "All OFF");
          } else {
            snprintf(msg, sizeof(msg), "Relays %02X:%02X", mask, state);
          }
          lcd_set_event(msg);
          lcd_update_status();
        }
        break;
      case CAN_ID_RELAY_STATUS:
        if (f.dlc >= 1) { g_relay_mirror = f.data[0]; lcd_update_status(); }
        break;
      case CAN_ID_TELEMETRY:
        break;
      case CAN_ID_VIPER_CMD:
        if (f.dlc >= 1) {
          const char* action =
            (f.data[0] == VIPER_CMD_LOCK)        ? "Viper: Lock" :
            (f.data[0] == VIPER_CMD_UNLOCK)       ? "Viper: Unlock" :
            (f.data[0] == VIPER_CMD_REMOTE_START) ? "Viper: R.Start" : "Viper: CMD";
          lcd_set_event(action);
        }
        break;
      case CAN_ID_VIPER_STATUS:
        lcd_set_event("Viper: Response");
        break;
      case CAN_ID_CONFIG_WRITE:    handle_cfg_write(f); break;
      case CAN_ID_CONFIG_READ_REQ: handle_cfg_read(f);  break;
      case CAN_ID_CONFIG_SAVE:     handle_cfg_save(f);  break;
      case CAN_ID_LCD_CMD:         handle_lcd_cmd(f);   break;
      default: break;
    }
  }

  static uint32_t last_twai_check = 0;
  uint32_t now_ms = millis();
  if (now_ms - last_twai_check >= 2000) {
    last_twai_check = now_ms;
    twai_status_info_t info;
    if (twai_get_status_info(&info) == ESP_OK) {
      g_can_ok = (info.state == TWAI_STATE_RUNNING);
      wlog("[twai] state=%d tx_err=%u rx_err=%u tx_failed=%u rx_missed=%u\n",
                    info.state, info.tx_error_counter, info.rx_error_counter,
                    info.tx_failed_count, info.rx_missed_count);
      lcd_update_status();
      if (info.state == TWAI_STATE_BUS_OFF) {
        wlogln("[bus] TWAI BUS_OFF — recovering");
        twai_initiate_recovery();
      }
    }
  }

  delay(1);
}
