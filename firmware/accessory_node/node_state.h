// node_state.h — shared runtime state visible to all modules.
// Defined in accessory_node.ino; each module that needs them declares extern here.

#pragma once
#include <stdint.h>

extern uint8_t g_relay_mirror;  // relay states as last seen on the bus
extern bool    g_can_ok;        // CAN bus health (updated by TWAI health check)
extern bool    g_menu_active;   // true while the LCD menu is open (set by mod_menu)
