#pragma once
#include "node_config.h"

#ifdef ENABLE_M5_CARDPUTER
#include "bus.h"
void m5_setup();
void m5_loop();
void m5_handle_frame(const BusFrame& f);
#else
static inline void m5_setup() {}
static inline void m5_loop()  {}
static inline void m5_handle_frame(const BusFrame&) {}
#endif
