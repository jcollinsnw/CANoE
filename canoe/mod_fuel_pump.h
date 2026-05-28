#pragma once
#include "node_config.h"
#include <stdint.h>

#ifdef ENABLE_FUEL_PUMP_SAFETY
#include "bus.h"
void fuel_pump_setup();
void fuel_pump_loop();
void fuel_pump_handle_frame(const BusFrame& f);
// Set the gate bitmask (FP_MODE_*). Also reachable via CONFIG_WRITE
// CFG_KEY_FUEL_PUMP_SAFETY and ACT_FUEL_PUMP_SAFETY_* rule actions. Not
// persisted; resets to FUEL_PUMP_DEFAULT_MODE on every reboot.
void    fuel_pump_set_mode(uint8_t mode);
uint8_t fuel_pump_mode();
// Back-compat boolean API — maps to FP_MODE_RPM (the only gate that
// existed before COIL was added) when enabled.
void    fuel_pump_set_safety_enabled(bool enabled);
bool    fuel_pump_safety_enabled();
// Coil voltage sense (folded in from the old mod_ignition). Returns the
// hysteresis-debounced on/off state and the last sampled centivolts. Only
// meaningful when IGN_COIL_ADC_PIN is defined in the node config.
bool    fuel_pump_coil_on();
int16_t fuel_pump_coil_cv();
#else
static inline void    fuel_pump_setup() {}
static inline void    fuel_pump_loop()  {}
static inline void    fuel_pump_handle_frame(const BusFrame&) {}
static inline void    fuel_pump_set_mode(uint8_t) {}
static inline uint8_t fuel_pump_mode()  { return 0; }
static inline void    fuel_pump_set_safety_enabled(bool) {}
static inline bool    fuel_pump_safety_enabled() { return false; }
static inline bool    fuel_pump_coil_on() { return false; }
static inline int16_t fuel_pump_coil_cv() { return 0; }
#endif
