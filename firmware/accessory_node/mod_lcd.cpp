// mod_lcd.cpp — HD44780 16x2 LCD via PCF8574 I2C backpack driver.

#include <Arduino.h>
#include <Wire.h>
#include "node_config.h"

#ifdef ENABLE_LCD

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

#define LCD_BL 0x08   // backlight bit in PCF8574 byte
#define LCD_EN 0x04   // enable strobe
#define LCD_RS 0x01   // register select: 0 = command, 1 = data

static bool g_lcd_backlight = true;

// --------------------------------------------------------------
// Widget registry
// --------------------------------------------------------------
static LcdWidget g_widgets[LCD_MAX_WIDGETS];
static uint8_t   g_widget_count = 0;
static uint32_t  g_widget_last_render[LCD_MAX_WIDGETS] = {};

void lcd_register_widget(const LcdWidget& w) {
  if (g_widget_count < LCD_MAX_WIDGETS)
    g_widgets[g_widget_count++] = w;
}

// Render all registered widgets in a given row, or all rows if row == 0xFF.
static void render_widgets(uint8_t row_filter) {
  char buf[LCD_COLS + 1];
  for (uint8_t i = 0; i < g_widget_count; i++) {
    if (row_filter != 0xFF && g_widgets[i].row != row_filter) continue;
    uint8_t w = g_widgets[i].width;
    if (w > LCD_COLS) w = LCD_COLS;
    memset(buf, ' ', w);
    g_widgets[i].render(buf, w);
    lcd_set_cursor(g_widgets[i].row, g_widgets[i].col);
    lcd_print_n(buf, w);
    g_widget_last_render[i] = millis();
  }
}

// Return the leftmost widget col on a given row, or LCD_COLS if none.
static uint8_t widget_boundary(uint8_t row) {
  uint8_t bound = LCD_COLS;
  for (uint8_t i = 0; i < g_widget_count; i++)
    if (g_widgets[i].row == row && g_widgets[i].col < bound)
      bound = g_widgets[i].col;
  return bound;
}

// --------------------------------------------------------------
// Per-relay icons and labels (populated from config macros at setup)
// --------------------------------------------------------------
static uint8_t g_icon_on[6];   // CGRAM slot, 0xFF = no custom icon
static uint8_t g_icon_off[6];

// Shared status indicator icons (CAN and WiFi use the same on/off bitmaps).
// Slots allocated at the end of the relay-icon chain in lcd_setup().
static uint8_t g_status_on_slot  = 0xFF;  // filled icon  = link up
static uint8_t g_status_off_slot = 0xFF;  // hollow icon  = link down

static const uint8_t STATUS_ICON_ON[8]  = {0b01110, 0b01010, 0b01010, 0b01010,
                                            0b01110, 0b01110, 0b01110, 0b01110};
static const uint8_t STATUS_ICON_OFF[8] = {0b01110, 0b01110, 0b01110, 0b01110,
                                            0b01010, 0b01010, 0b01010, 0b01110};

static const char* const g_relay_label[6] = {
#ifdef RELAY_1_LABEL
  RELAY_1_LABEL,
#else
  nullptr,
#endif
#ifdef RELAY_2_LABEL
  RELAY_2_LABEL,
#else
  nullptr,
#endif
#ifdef RELAY_3_LABEL
  RELAY_3_LABEL,
#else
  nullptr,
#endif
#ifdef RELAY_4_LABEL
  RELAY_4_LABEL,
#else
  nullptr,
#endif
#ifdef RELAY_5_LABEL
  RELAY_5_LABEL,
#else
  nullptr,
#endif
#ifdef RELAY_6_LABEL
  RELAY_6_LABEL,
#else
  nullptr,
#endif
};

// --------------------------------------------------------------
// Low-level driver (private)
// --------------------------------------------------------------
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
static void lcd_cmd_raw(uint8_t c)  { lcd_byte(c, false); }
static void lcd_data_raw(uint8_t c) { lcd_byte(c, true);  }

// Write one 8-row custom character into CGRAM slot (0–7).
// Caller must restore DDRAM address afterward.
static void write_cgram(uint8_t slot, const uint8_t pattern[8]) {
  lcd_cmd_raw(0x40 | (slot << 3));
  for (uint8_t i = 0; i < 8; i++) lcd_data_raw(pattern[i]);
}

// Helper macro — load one icon into CGRAM if the config macro is defined.
#define _LOAD_ICON(relay_idx, macro, slot_arr) \
  if (cgram_slot < 8) { \
    static const uint8_t _d[] = macro; \
    write_cgram(cgram_slot, _d); \
    slot_arr[relay_idx] = cgram_slot++; \
  }

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------
void lcd_set_cursor(uint8_t row, uint8_t col) {
  static const uint8_t ROW_OFF[] = { 0x00, 0x40 };
  lcd_cmd_raw(0x80 | (ROW_OFF[row & 1] + (col & 0x0F)));
}

void lcd_clear() { lcd_cmd_raw(0x01); delay(2); }

void lcd_print_n(const char* s, uint8_t len) {
  for (uint8_t i = 0; i < len && s[i]; i++) lcd_data_raw((uint8_t)s[i]);
}

void lcd_write_row(uint8_t row, const char* text) {
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "%-16s", text);
  lcd_set_cursor(row, 0);
  lcd_print_n(buf, LCD_COLS);
}

void lcd_set_backlight(bool on) { g_lcd_backlight = on; }
bool lcd_get_backlight()        { return g_lcd_backlight; }

void lcd_setup() {
  delay(50);
  // HD44780 4-bit power-on init sequence (per datasheet)
  lcd_nibble(0x03, false); delay(5);
  lcd_nibble(0x03, false); delayMicroseconds(150);
  lcd_nibble(0x03, false);
  lcd_nibble(0x02, false);   // enter 4-bit mode
  lcd_cmd_raw(0x28);         // 4-bit, 2 lines, 5×8 dots
  lcd_cmd_raw(0x08);         // display off
  lcd_clear();
  lcd_cmd_raw(0x06);         // entry mode: increment, no shift
  lcd_cmd_raw(0x0C);         // display on, cursor off

  // Load custom CGRAM icons defined in node_config.h.
  // Allocation starts at slot 1 — slot 0 maps to '\x00' (C null terminator)
  // which would silently truncate snprintf output. Slots 1–7 are safe.
  // Max 7 custom icons total across all relays.
  memset(g_icon_on,  0xFF, sizeof(g_icon_on));
  memset(g_icon_off, 0xFF, sizeof(g_icon_off));
  uint8_t cgram_slot = 1;
#ifdef RELAY_1_ICON_ON
  _LOAD_ICON(0, RELAY_1_ICON_ON,  g_icon_on)
#endif
#ifdef RELAY_1_ICON_OFF
  _LOAD_ICON(0, RELAY_1_ICON_OFF, g_icon_off)
#endif
#ifdef RELAY_2_ICON_ON
  _LOAD_ICON(1, RELAY_2_ICON_ON,  g_icon_on)
#endif
#ifdef RELAY_2_ICON_OFF
  _LOAD_ICON(1, RELAY_2_ICON_OFF, g_icon_off)
#endif
#ifdef RELAY_3_ICON_ON
  _LOAD_ICON(2, RELAY_3_ICON_ON,  g_icon_on)
#endif
#ifdef RELAY_3_ICON_OFF
  _LOAD_ICON(2, RELAY_3_ICON_OFF, g_icon_off)
#endif
#ifdef RELAY_4_ICON_ON
  _LOAD_ICON(3, RELAY_4_ICON_ON,  g_icon_on)
#endif
#ifdef RELAY_4_ICON_OFF
  _LOAD_ICON(3, RELAY_4_ICON_OFF, g_icon_off)
#endif
#ifdef RELAY_5_ICON_ON
  _LOAD_ICON(4, RELAY_5_ICON_ON,  g_icon_on)
#endif
#ifdef RELAY_5_ICON_OFF
  _LOAD_ICON(4, RELAY_5_ICON_OFF, g_icon_off)
#endif
#ifdef RELAY_6_ICON_ON
  _LOAD_ICON(5, RELAY_6_ICON_ON,  g_icon_on)
#endif
#ifdef RELAY_6_ICON_OFF
  _LOAD_ICON(5, RELAY_6_ICON_OFF, g_icon_off)
#endif
  // Status indicator icons — loaded after relay icons so they never displace them.
  if (cgram_slot < 8) { write_cgram(cgram_slot, STATUS_ICON_ON);  g_status_on_slot  = cgram_slot++; }
  if (cgram_slot < 8) { write_cgram(cgram_slot, STATUS_ICON_OFF); g_status_off_slot = cgram_slot++; }

  if (cgram_slot > 0)
    lcd_cmd_raw(0x80);  // return to DDRAM address 0 after CGRAM writes
  wlog("[LCD] init OK (%u custom icon%s)\n", cgram_slot - 1, cgram_slot == 2 ? "" : "s");
}

void lcd_update_status() {
  if (g_menu_active) return;

  // CAN and WiFi status icons — fall back to ASCII if CGRAM slots weren't allocated.
  bool have_icons = (g_status_on_slot != 0xFF && g_status_off_slot != 0xFF);
  auto status_char = [&](bool ok) -> char {
    return have_icons ? (char)(ok ? g_status_on_slot : g_status_off_slot)
                      : (ok ? '+' : '-');
  };

#if USE_WIFI
  bool wifi_ok = bus_wifi_seen_peer();
#else
  bool wifi_ok = false;
#endif

  char relays[7];
  for (uint8_t i = 0; i < 6; i++) {
    bool on = (g_relay_mirror & (1 << i)) != 0;
    relays[i] = lcd_relay_char(i, on);
  }
  relays[6] = '\0';

  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "C%cW%c [%s]",
           status_char(g_can_ok), status_char(wifi_ok), relays);
  lcd_write_row(0, buf);

  render_widgets(0xFF);
}

char lcd_relay_char(uint8_t idx, bool on) {
  if (idx < 6) {
    uint8_t slot = on ? g_icon_on[idx] : g_icon_off[idx];
    if (slot != 0xFF) return (char)slot;
  }
  return on ? (char)0xFF : '-';
}

const char* lcd_relay_label(uint8_t idx) {
  static const char* const defaults[] = {
    "Relay 1", "Relay 2", "Relay 3", "Relay 4", "Relay 5", "Relay 6"
  };
  if (idx < 6 && g_relay_label[idx]) return g_relay_label[idx];
  return (idx < 6) ? defaults[idx] : "Relay ?";
}

void lcd_set_event(const char* msg) {
  if (g_menu_active) return;
  // Only write up to the first widget boundary on row 1 so widget regions are preserved.
  uint8_t text_width = widget_boundary(1);
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "%-*s", text_width, msg);
  lcd_set_cursor(1, 0);
  lcd_print_n(buf, text_width);
  render_widgets(1);
}

void lcd_tick() {
  if (g_menu_active) return;
  uint32_t now = millis();
  char buf[LCD_COLS + 1];
  for (uint8_t i = 0; i < g_widget_count; i++) {
    if (g_widgets[i].refresh_ms == 0) continue;
    if (now - g_widget_last_render[i] < g_widgets[i].refresh_ms) continue;
    uint8_t w = g_widgets[i].width;
    if (w > LCD_COLS) w = LCD_COLS;
    memset(buf, ' ', w);
    g_widgets[i].render(buf, w);
    lcd_set_cursor(g_widgets[i].row, g_widgets[i].col);
    lcd_print_n(buf, w);
    g_widget_last_render[i] = now;
  }
}

void lcd_handle_frame(const BusFrame& f) {
  switch (f.id) {
    case CAN_ID_LCD_CMD:
      if (f.dlc < 1) break;
      if (f.data[0] == 0xFF) { lcd_clear(); break; }
      if (f.data[0] < LCD_ROWS && f.dlc >= 2) {
        lcd_set_cursor(f.data[0], f.data[1]);
        if (f.dlc > 2) lcd_print_n((const char*)&f.data[2], f.dlc - 2);
      }
      break;

    case CAN_ID_VIPER_CMD:
      if (f.dlc >= 1) {
        const char* action =
          (f.data[0] == VIPER_CMD_LOCK)        ? "Viper: Lock"    :
          (f.data[0] == VIPER_CMD_UNLOCK)       ? "Viper: Unlock"  :
          (f.data[0] == VIPER_CMD_REMOTE_START) ? "Viper: R.Start" : "Viper: CMD";
        lcd_set_event(action);
      }
      break;

    case CAN_ID_VIPER_STATUS:
      break;

    default: break;
  }
}

#endif // ENABLE_LCD
