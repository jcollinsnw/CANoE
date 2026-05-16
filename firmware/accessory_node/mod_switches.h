#pragma once
#include "node_config.h"

#ifdef ENABLE_SWITCHES
#include "bus.h"

void switches_setup();
void switches_loop();
void switches_handle_ack(const BusFrame&);
void switches_clear_pending();

#else
inline void switches_handle_ack(const BusFrame&) {}
inline void switches_clear_pending() {}
#endif // ENABLE_SWITCHES
