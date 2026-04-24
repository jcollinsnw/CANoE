#pragma once
#include "node_config.h"

#ifdef ENABLE_RPM
#include "bus.h"

void rpm_setup();
void rpm_loop();
void rpm_handle_frame(const BusFrame& f);

#else

static inline void rpm_setup() {}
static inline void rpm_loop()  {}

#endif // ENABLE_RPM
