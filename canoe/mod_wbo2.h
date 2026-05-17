#pragma once
#include "node_config.h"

#ifdef ENABLE_WBO2
#include "bus.h"

void wbo2_setup();
void wbo2_loop();
void wbo2_handle_frame(const BusFrame& f);

#else

static inline void wbo2_setup()                       {}
static inline void wbo2_loop()                        {}
static inline void wbo2_handle_frame(const BusFrame&) {}

#endif // ENABLE_WBO2
