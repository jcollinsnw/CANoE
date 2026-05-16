#pragma once
#include "node_config.h"

#ifdef ENABLE_M5_CARDPUTER
#include "bus.h"
void m5_setup();
void m5_loop();
void m5_handle_frame(const BusFrame& f);
void m5_set_event(const char* msg);
void m5_beep_startup();
void m5_beep_alert();
void m5_beep_can_up();
void m5_beep_can_down();
void m5_beep_peer(uint8_t count);
#else
static inline void m5_setup() {}
static inline void m5_loop()  {}
static inline void m5_handle_frame(const BusFrame&) {}
static inline void m5_set_event(const char*) {}
static inline void m5_beep_startup() {}
static inline void m5_beep_alert() {}
static inline void m5_beep_can_up() {}
static inline void m5_beep_can_down() {}
static inline void m5_beep_peer(uint8_t) {}
#endif
