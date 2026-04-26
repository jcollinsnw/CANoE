#pragma once
#include "node_config.h"

#ifdef ENABLE_ECU
#include "bus.h"

void ecu_setup();
void ecu_loop();
void ecu_handle_frame(const BusFrame& f);

#else

static inline void ecu_setup()                       {}
static inline void ecu_loop()                        {}
static inline void ecu_handle_frame(const BusFrame&) {}

#endif // ENABLE_ECU
