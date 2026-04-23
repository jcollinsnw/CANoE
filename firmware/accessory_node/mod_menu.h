#pragma once
#include "node_config.h"

// mod_menu.h — LCD menu system.
//
// Configured by node_config.h:
//   ENABLE_MENU          — compile in the menu (requires ENABLE_LCD)
//   MENU_HAS_RELAYS      — show Relays submenu
//   MENU_HAS_VIPER       — show Viper submenu
//   MENU_HAS_BUS         — show Bus Status submenu
//   MENU_HAS_DISPLAY     — show Display submenu (backlight)
//   MENU_HAS_WIFI        — show WiFi submenu (per-node enable/disable)
//
// Input wiring: assign SW_ACT_MENU_NAV to whichever button should drive the
// menu (short press = select/navigate, long press = enter/confirm). The
// rotary encoder (if present) scrolls through items when the menu is open.

#ifdef ENABLE_MENU

void menu_setup();            // build item list, seed state from NVS — call once from setup()
void menu_tick();             // auto-exit timeout — call from loop()
void menu_enter();            // open the menu
void menu_exit();             // close the menu
void menu_scroll(int8_t dir); // rotate: +1 = CW / next, -1 = CCW / prev
void menu_select();           // short press: navigate into submenu or back
void menu_action();           // long press: execute current item
bool menu_is_active();        // true while menu is displayed

#else

static inline void menu_setup()           {}
static inline void menu_tick()            {}
static inline void menu_enter()           {}
static inline void menu_exit()            {}
static inline void menu_scroll(int8_t)    {}
static inline void menu_select()          {}
static inline void menu_action()          {}
static inline bool menu_is_active()       { return false; }

#endif // ENABLE_MENU
