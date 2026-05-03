#pragma once
#include "node_config.h"

#ifdef ENABLE_SWITCHES
#include "bus.h"

void switches_setup();
void switches_loop();                      // call every loop(); polls inputs + advances retry timers
void switches_handle_ack(const BusFrame&); // call on CAN_ID_SWITCH_ACK frames to clear pending retries

#else
inline void switches_handle_ack(const BusFrame&) {}
#endif // ENABLE_SWITCHES
