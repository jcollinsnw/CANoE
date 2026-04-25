#pragma once
#include "node_config.h"

#ifndef LCD_COLS
#define LCD_COLS 16
#endif
#ifndef LCD_ROWS
#define LCD_ROWS 2
#endif

// --------------------------------------------------------------
// LCD Widget system — available regardless of ENABLE_LCD so that
// modules can reference LcdWidget in their headers without guards.
// --------------------------------------------------------------
#define LCD_MAX_WIDGETS 8
#define LCD_MAX_RELAYS  8

struct LcdWidget {
  uint8_t  row;           // LCD row (0 or 1)
  uint8_t  col;           // starting column
  uint8_t  width;         // character width of this widget's region
  uint16_t refresh_ms;    // re-render interval; 0 = only on lcd_update_status()
  void   (*render)(char* buf, uint8_t width);  // fill buf[0..width-1], no null needed
};

#ifdef ENABLE_LCD
#include "bus.h"

void lcd_setup();
void lcd_update_status();           // redraws row 0 + all registered widgets
void lcd_set_event(const char* msg);// writes row 1 text; respects widget boundaries; no-op while menu is active
void lcd_write_row(uint8_t row, const char* text);
void lcd_set_cursor(uint8_t row, uint8_t col);
void lcd_print_n(const char* s, uint8_t len);
void lcd_clear();
void lcd_set_backlight(bool on);
bool lcd_get_backlight();
void lcd_handle_frame(const BusFrame& f);
void lcd_tick();                    // call from loop(); re-renders widgets on their refresh schedule

void lcd_register_widget(const LcdWidget& w);  // register a display widget; call from module setup

// CGRAM icon allocation — call after lcd_setup(), before first lcd_update_status().
// Writes pattern into the next free CGRAM slot (1–7). Returns the slot or 0xFF if full.
uint8_t lcd_alloc_cgram(const uint8_t pattern[8]);

// Status icon helper — returns the filled/hollow CGRAM glyph (or '+'/'-' ASCII fallback).
// Used by bus.cpp CAN/WiFi widget renderers which live outside mod_lcd.
char lcd_status_char(bool ok);

// Per-relay display helpers (used by relay widget and menu; implemented in relay_icons.cpp).
char        lcd_relay_char(uint8_t relay_idx, bool on);
const char* lcd_relay_label(uint8_t relay_idx);

#else
// No-op stubs so other modules compile cleanly when LCD is absent.
static inline void lcd_update_status()                {}
static inline void lcd_set_event(const char*)          {}
static inline void lcd_write_row(uint8_t, const char*) {}
static inline void lcd_set_cursor(uint8_t, uint8_t)   {}
static inline void lcd_print_n(const char*, uint8_t)  {}
static inline void lcd_clear()                         {}
static inline void lcd_set_backlight(bool)             {}
static inline bool lcd_get_backlight()                 { return false; }
static inline void lcd_tick()                              {}
static inline void lcd_register_widget(const LcdWidget&)   {}
static inline uint8_t lcd_alloc_cgram(const uint8_t*)      { return 0xFF; }
static inline char lcd_status_char(bool)                   { return '-'; }
static inline char lcd_relay_char(uint8_t, bool on)       { return on ? (char)0xFF : '-'; }
static inline const char* lcd_relay_label(uint8_t)        { return "Relay ?"; }

#endif // ENABLE_LCD
