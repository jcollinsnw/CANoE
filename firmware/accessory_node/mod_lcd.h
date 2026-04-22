#pragma once
#include "node_config.h"

#ifndef LCD_COLS
#define LCD_COLS 16
#endif
#ifndef LCD_ROWS
#define LCD_ROWS 2
#endif

#ifdef ENABLE_LCD
#include "bus.h"

void lcd_setup();
void lcd_update_status();           // redraws row 0 (CAN health + relay bitmap)
void lcd_set_event(const char* msg);// writes row 1; no-op while menu is active
void lcd_write_row(uint8_t row, const char* text);
void lcd_set_cursor(uint8_t row, uint8_t col);
void lcd_print_n(const char* s, uint8_t len);
void lcd_clear();
void lcd_set_backlight(bool on);
bool lcd_get_backlight();
void lcd_handle_frame(const BusFrame& f); // LCD_CMD, VIPER_CMD/STATUS display

#else
// No-op stubs so other modules compile cleanly when LCD is absent.
static inline void lcd_update_status()              {}
static inline void lcd_set_event(const char*)        {}
static inline void lcd_write_row(uint8_t, const char*) {}
static inline void lcd_set_cursor(uint8_t, uint8_t) {}
static inline void lcd_print_n(const char*, uint8_t){}
static inline void lcd_clear()                       {}
static inline void lcd_set_backlight(bool)           {}
static inline bool lcd_get_backlight()               { return false; }

#endif // ENABLE_LCD
