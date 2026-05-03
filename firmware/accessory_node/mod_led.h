#pragma once
#include "node_config.h"
#include "bus.h"

#ifdef ENABLE_LEDS

void led_setup();
void led_handle_frame(const BusFrame& f);
void led_tick();

#else

inline void led_setup() {}
inline void led_handle_frame(const BusFrame&) {}
inline void led_tick() {}

#endif
