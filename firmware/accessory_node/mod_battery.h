#pragma once
#include "node_config.h"

#ifdef ENABLE_BATTERY
#include "bus.h"
void battery_setup();
void battery_loop();
#else
static inline void battery_setup() {}
static inline void battery_loop()  {}
#endif
