// mod_error.h — unified error event emitter and receiver.
//
// Any module can call error_raise() / error_clear() to broadcast an
// ERROR_EVENT (0x0F6) frame. Receiver nodes react based on severity:
//   - Buzzer: INFO=silent, WARNING=short beep, CRITICAL=alert, EMERGENCY=fuel_pump_off tone
//   - LEDs: WARNING=flash, CRITICAL/EMERGENCY=solid until cleared
//   - Cardputer: tone + FEED event + CLI bar message
//
// Latching errors persist until an explicit error_clear() is received.
// Non-latching errors auto-clear after ERR_AUTO_CLEAR_MS (default 10 s).

#pragma once
#include "node_config.h"
#include "bus.h"
#include "can_protocol.h"

// How long a non-latching active error persists before auto-clearing (ms)
#ifndef ERR_AUTO_CLEAR_MS
#define ERR_AUTO_CLEAR_MS 10000
#endif

// Maximum simultaneously active errors tracked per node
#ifndef ERR_MAX_ACTIVE
#define ERR_MAX_ACTIVE 8
#endif

// Active error slot
struct ActiveError {
    uint8_t  source_node;   // which node raised it
    uint8_t  error_code;    // ERR_* code
    uint8_t  severity;      // ERR_SEV_*
    uint8_t  flags;         // ERR_FLAG_*
    uint32_t raised_at;     // millis() when first received
};

// --- Emitter API (call from any module) ---

// Raise an error: broadcasts ERROR_EVENT to all nodes.
// target: 0xFF = all nodes should react; specific node_id = only that node alerts.
// Returns immediately; the local node also reacts via its own self-echo handler.
void error_raise(uint8_t error_code, uint8_t severity, uint8_t target,
                 uint8_t flags, uint8_t arg0 = 0, uint8_t arg1 = 0, uint8_t arg2 = 0);

// Clear a previously raised error (emits ERROR_EVENT with ACTIVE=0).
void error_clear(uint8_t error_code);

// Convenience: raise with default flags derived from severity
//   WARNING  → AUDIBLE | VISUAL
//   CRITICAL → AUDIBLE | VISUAL | LATCHING
//   EMERGENCY→ AUDIBLE | VISUAL | LATCHING
inline void error_raise_local(uint8_t error_code, uint8_t severity,
                              uint8_t arg0 = 0, uint8_t arg1 = 0, uint8_t arg2 = 0) {
    uint8_t flags = ERR_FLAG_ACTIVE;
    if (severity >= ERR_SEV_WARNING)  flags |= ERR_FLAG_AUDIBLE | ERR_FLAG_VISUAL;
    if (severity >= ERR_SEV_CRITICAL) flags |= ERR_FLAG_LATCHING;
    error_raise(error_code, severity, 0xFF, flags, arg0, arg1, arg2);
}

// --- Receiver API (called from frame dispatch in canoe.ino) ---

// Process an incoming ERROR_EVENT frame. Manages active error table,
// triggers buzzer/LED/display reactions.
void error_handle_frame(const BusFrame& f);

// Periodic tick — auto-clears expired non-latching errors.
void error_tick();

// Query: is any error at or above the given severity currently active?
bool error_any_active(uint8_t min_severity = ERR_SEV_WARNING);

// Query: is a specific error code currently active?
bool error_is_active(uint8_t error_code);

// Get the highest active severity (returns 0xFF if no errors active).
uint8_t error_max_severity();

// Human-readable name for an error code (for FEED screen / serial log).
const char* error_code_name(uint8_t code);
