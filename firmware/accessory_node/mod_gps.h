#pragma once
#include "node_config.h"

#ifdef ENABLE_GPS

void gps_setup();
void gps_loop();

#else

static inline void gps_setup() {}
static inline void gps_loop()  {}

#endif // ENABLE_GPS
