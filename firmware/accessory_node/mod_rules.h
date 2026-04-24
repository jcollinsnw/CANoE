#pragma once
#include "node_config.h"

#ifdef ENABLE_RULES
#include "bus.h"
#include "can_protocol.h"

void rules_setup();
void rules_handle_frame(const BusFrame& f);

// Web UI / HTTP API accessors
uint8_t      rules_max();
CanRule      rules_get(uint8_t idx);
void         rules_set(uint8_t idx, const CanRule& r);  // writes + saves to NVS
void         rules_clear(uint8_t idx);                  // zeros slot + saves
void         rules_reset_factory();                      // reload compiled defaults

#endif // ENABLE_RULES
