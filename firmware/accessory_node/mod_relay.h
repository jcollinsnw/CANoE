#pragma once
#include "node_config.h"

#ifdef ENABLE_RELAY
#include "bus.h"

void relay_setup();
void relay_loop();
void relay_handle_frame(const BusFrame& f);

#endif // ENABLE_RELAY
