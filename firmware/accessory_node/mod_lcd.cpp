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

static bool    g_lcd_backlight = true;
static char    g_event_buf[LCD_COLS + 1] = {};  // current event row text (padded, no null needed)
static uint8_t g_cgram_next = 1;               // next free CGRAM slot (1–7; slot 0 is reserved)
static uint8_t g_cgram_patterns[8][8] = {};    // stored for re-init after LCD power glitch
static uint8_t g_i2c_fail_count = 0;           // consecutive I2C write failures

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

// Return the leftmost widget col on a given row for widgets with auto-refresh (refresh_ms > 0).
// Used externally; event/background widgets (refresh_ms == 0) are excluded.
static uint8_t widget_boundary(uint8_t row) {
  uint8_t bound = LCD_COLS;
  for (uint8_t i = 0; i < g_widget_count; i++)
    if (g_widgets[i].row == row && g_widgets[i].refresh_ms > 0 && g_widgets[i].col < bound)
      bound = g_widgets[i].col;
  return bound;
}

// --------------------------------------------------------------
// Status indicator icons (CAN / WiFi) — loaded by lcd_setup(), LCD's own concern.
// --------------------------------------------------------------
static uint8_t g_status_on_slot  = 0xFF;  // filled icon  = link up
static uint8_t g_status_off_slot = 0xFF;  // hollow icon  = link down

static const uint8_t STATUS_ICON_ON[8]  = {0b01110, 0b01010, 0b01010, 0b01010,
                                            0b01110, 0b01110, 0b01110, 0b01110};
static const uint8_t STATUS_ICON_OFF[8] = {0b01110, 0b01110, 0b01110, 0b01110,
                                            0b01010, 0b01010, 0b01010, 0b01110};

// --------------------------------------------------------------
// Low-level driver (private)
// --------------------------------------------------------------
static void lcd_i2c_write(uint8_t v) {
  Wire.beginTransmission(LCD_I2C_ADDR);
  Wire.write(v | (g_lcd_backlight ? LCD_BL : 0));
  if (Wire.endTransmission() != 0) {
    if (g_i2c_fail_count < 255) g_i2c_fail_count++;
  } else {
    g_i2c_fail_count = 0;
  }
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
// Caller must restore DDRAM address afterward (lcd_alloc_cgram does this).
static void write_cgram(uint8_t slot, const uint8_t pattern[8]) {
  lcd_cmd_raw(0x40 | (slot << 3));
  for (uint8_t i = 0; i < 8; i++) lcd_data_raw(pattern[i]);
}

// --------------------------------------------------------------
// Built-in widget render functions
// --------------------------------------------------------------

// Event row: copies g_event_buf into the render buffer (registered at row 1, full width).
static void event_render(char* buf, uint8_t width) {
  uint8_t n = strnlen(g_event_buf, LCD_COLS);
  memcpy(buf, g_event_buf, n < width ? n : width);
}

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------

uint8_t lcd_alloc_cgram(const uint8_t pattern[8]) {
  if (g_cgram_next >= 8) return 0xFF;
  memcpy(g_cgram_patterns[g_cgram_next], pattern, 8);
  write_cgram(g_cgram_next, pattern);
  lcd_cmd_raw(0x80);  // return to DDRAM after CGRAM write
  return g_cgram_next++;
}

char lcd_status_char(bool ok) {
  bool have_icons = (g_status_on_slot != 0xFF && g_status_off_slot != 0xFF);
  return have_icons ? (char)(ok ? g_status_on_slot : g_status_off_slot)
                    : (ok ? '+' : '-');
}

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

// Replay the full HD44780 power-on init + CGRAM rewrite.
// Returns true if all I2C writes succeeded (fail count stayed at 0).
static bool lcd_hard_reinit(const char* reason) {
  g_i2c_fail_count = 0;
  delay(50);
  lcd_nibble(0x03, false); delay(5);
  lcd_nibble(0x03, false); delayMicroseconds(150);
  lcd_nibble(0x03, false);
  lcd_nibble(0x02, false);
  lcd_cmd_raw(0x28);
  lcd_cmd_raw(0x08);
  lcd_clear();
  lcd_cmd_raw(0x06);
  lcd_cmd_raw(0x0C);
  for (uint8_t s = 1; s < g_cgram_next; s++)
    write_cgram(s, g_cgram_patterns[s]);
  lcd_cmd_raw(0x80);
  lcd_write_row(0, "");
  render_widgets(0xFF);
  bool ok = (g_i2c_fail_count == 0);
  wlog("[LCD] reinit (%s) %s\n", reason, ok ? "OK" : "I2C not responding");
  g_i2c_fail_count = 0;  // clear again so failed reinit writes don't immediately retrigger
  return ok;
}

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

  // Status indicator icons are LCD's own concern — always allocated first so they
  // are guaranteed slots 1 and 2 regardless of how many relay icons follow.
  g_status_on_slot  = lcd_alloc_cgram(STATUS_ICON_ON);
  g_status_off_slot = lcd_alloc_cgram(STATUS_ICON_OFF);

  wlog("[LCD] init OK (status icons at slots %u/%u, %u CGRAM slots free)\n",
       g_status_on_slot, g_status_off_slot, (uint8_t)(8 - g_cgram_next));

  // Blank row 0 so gap areas between widgets stay clean.
  lcd_write_row(0, "");

  // Register the event text widget (row 1, full width, background — no auto-refresh).
  // relay_icons_init() registers the relay widget on top of this after lcd_setup() returns.
  memset(g_event_buf, ' ', LCD_COLS);
  { LcdWidget w = { 1, 0, LCD_COLS, 0, event_render }; lcd_register_widget(w); }
}

void lcd_update_status() {
  if (g_menu_active) return;
  render_widgets(0xFF);
}

void lcd_set_event(const char* msg) {
  if (g_menu_active) return;
  snprintf(g_event_buf, LCD_COLS + 1, "%-*s", LCD_COLS, msg);
  render_widgets(1);
}

void lcd_tick() {
  if (g_menu_active) return;
  uint32_t now = millis();

  static uint32_t last_reinit_ms  = 0;
  static bool     last_reinit_ok  = true;   // was the most recent reinit attempt successful?

  // Error-triggered reinit: only when the previous attempt worked (don't spam if hardware
  // is absent or address is wrong) and at least 5 s have passed since the last attempt.
  bool error_due    = (g_i2c_fail_count >= 5) && last_reinit_ok && (now - last_reinit_ms >= 5000);
  // Periodic reinit every 30 s handles HD44780 brownouts the PCF8574 can't detect.
  bool periodic_due = (now - last_reinit_ms) >= 30000;

  if (error_due || periodic_due) {
    last_reinit_ms = now;
    last_reinit_ok = lcd_hard_reinit(error_due ? "errors" : "periodic");
    return;
  }

  // If errors accumulated but we're still in the cooldown window, cap the counter so
  // the check above doesn't spin on every tick.
  if (g_i2c_fail_count >= 5) g_i2c_fail_count = 4;

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
    case CAN_ID_RELAY_STATUS:
      lcd_update_status();
      break;

    case CAN_ID_RELAY_CMD:
      if (f.dlc >= 2) {
        char msg[LCD_COLS + 1];
        uint8_t mask = f.data[0], state = f.data[1];
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
