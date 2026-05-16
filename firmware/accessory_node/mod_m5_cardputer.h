#pragma once
#include "node_config.h"

#ifdef ENABLE_M5_CARDPUTER
#include "bus.h"
void m5_setup();
void m5_loop();
void m5_handle_frame(const BusFrame& f);
void m5_set_event(const char* msg);
#else
static inline void m5_setup() {}
static inline void m5_loop()  {}
static inline void m5_handle_frame(const BusFrame&) {}
static inline void m5_set_event(const char*) {}
#endif
