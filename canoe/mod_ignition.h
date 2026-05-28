#pragma once
#include "node_config.h"

#ifdef ENABLE_IGNITION
void ignition_setup();
void ignition_loop();
// True when the last sampled coil voltage was above the ON threshold.
// Edge transitions also trigger an immediate IGNITION_DATA broadcast.
bool ignition_is_on();
int16_t ignition_coil_cv();
#else
static inline void ignition_setup() {}
static inline void ignition_loop()  {}
static inline bool ignition_is_on() { return false; }
static inline int16_t ignition_coil_cv() { return 0; }
#endif
