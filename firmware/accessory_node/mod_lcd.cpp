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
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
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
  wlogln("[LCD] init OK");
}

void lcd_update_status() {
  if (g_menu_active) return;
  char relays[7];
  for (uint8_t i = 0; i < 6; i++)
    relays[i] = (g_relay_mirror & (1 << i)) ? ('1' + i) : '-';
  relays[6] = '\0';
  char buf[LCD_COLS + 1];
  snprintf(buf, sizeof(buf), "CAN:%-3s [%s]", g_can_ok ? "OK" : "ERR", relays);
  lcd_write_row(0, buf);
}

void lcd_set_event(const char* msg) {
  if (g_menu_active) return;
  lcd_write_row(1, msg);
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
      lcd_set_event("Viper: Response");
      break;

    default: break;
  }
}

#endif // ENABLE_LCD
