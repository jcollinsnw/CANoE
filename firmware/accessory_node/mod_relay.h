#pragma once
#include "node_config.h"

// ---- Hardware relay control (relay controller node only) ----
#ifdef ENABLE_RELAY
#include "bus.h"
void relay_setup();
void relay_loop();
void relay_handle_frame(const BusFrame& f);
#endif

// ---- Relay LCD display (any node with ENABLE_LCD) ----
// Loads per-relay labels and CGRAM icons from node_config.h macros into mod_lcd's
// relay display registry. Call once from setup() after lcd_setup().
#ifdef ENABLE_LCD
void relay_icons_init();
uint8_t relay_lcd_count();  // number of relay slots shown on the LCD
#else
static inline void relay_icons_init()    {}
static inline uint8_t relay_lcd_count() { return 6; }
#endif
