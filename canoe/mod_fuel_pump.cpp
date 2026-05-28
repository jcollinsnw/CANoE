// mod_fuel_pump.cpp — multi-gate fuel pump safety FSM.
//
// On the antique car the master battery switch (chassis ground) keeps the
// relay node powered continuously, even key-out / parked. The OEM pump
// burned out the first time after the boot rule turned R1 on and the pump
// then dead-headed against a closed float valve for hours while the car sat.
//
// This FSM gates R1 on the union of zero or more "engine is alive" signals:
//   RPM gate    — ENGINE_DATA (0x304) rpm >= FUEL_PUMP_RPM_THRESHOLD
//   COIL gate   — IGNITION_DATA (0x310) ign_on == 1
//
// Modes (settable at runtime via CFG_KEY_FUEL_PUMP_SAFETY or rule action):
//   FP_MODE_OFF        — both gates disabled; pump forced ON
//   FP_MODE_RPM        — only RPM gate active
//   FP_MODE_COIL       — only COIL gate active
//   FP_MODE_BOTH       — RPM AND COIL must both pass
//
// FSM:
//   PRIME (pump ON, FUEL_PUMP_PRIME_MS)        → ARMED (timer elapsed)
//   ARMED (pump OFF, waiting for all enabled gates) → RUNNING (gates pass)
//   RUNNING (pump ON, monitoring gates)        → ARMED (gate stale STALL_MS)
//
// Advisory, not authoritative — the FSM emits a single RELAY_CMD on each
// state transition; manual relay toggles still work between transitions.
//
// On a stall (RUNNING → ARMED due to gate going stale, not user-initiated),
// the FSM broadcasts:
//   BUZZER_CMD (0x104) → broadcast w/ BUZZER_SEQ_FUEL_PUMP_OFF
//   FUEL_PUMP_STATE (0x311) with reason=STALL
//
// The switch panel's mod_buzzer reacts to BUZZER_CMD automatically; the
// Cardputer's mod_m5_cardputer handles both frames for audible + visible
// alerts.
//
// Config in node header:
//   ENABLE_FUEL_PUMP_SAFETY    (gate)
//   FUEL_PUMP_RELAY            (relay idx, default 0 / R1)
//   FUEL_PUMP_PRIME_MS         (default 3000)
//   FUEL_PUMP_RPM_THRESHOLD    (default 200)
//   FUEL_PUMP_STALL_MS         (default 2000)
//   FUEL_PUMP_DEFAULT_MODE     (default FP_MODE_BOTH = 3)

#include "node_config.h"

#ifdef ENABLE_FUEL_PUMP_SAFETY

#include <Arduino.h>
#include "can_protocol.h"
#include "bus.h"
#include "node_state.h"
#include "mod_fuel_pump.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#ifndef FUEL_PUMP_RELAY
#define FUEL_PUMP_RELAY 0
#endif
#ifndef FUEL_PUMP_PRIME_MS
#define FUEL_PUMP_PRIME_MS 3000
#endif
#ifndef FUEL_PUMP_RPM_THRESHOLD
#define FUEL_PUMP_RPM_THRESHOLD 200
#endif
#ifndef FUEL_PUMP_STALL_MS
#define FUEL_PUMP_STALL_MS 2000
#endif
#ifndef FUEL_PUMP_DEFAULT_MODE
#define FUEL_PUMP_DEFAULT_MODE FP_MODE_BOTH
#endif

enum class FpState : uint8_t { PRIME = 0, ARMED = 1, RUNNING = 2 };

// fuel_pump_state reason byte values — keep in sync with CAN_ID_FUEL_PUMP_STATE doc.
enum FpReason : uint8_t {
  FP_REASON_NONE        = 0,
  FP_REASON_BOOT        = 1,
  FP_REASON_PRIME_DONE  = 2,
  FP_REASON_GATE_PASS   = 3,
  FP_REASON_STALL       = 4,
  FP_REASON_MODE_CHANGE = 5,
  FP_REASON_RE_ENABLE   = 6,
};

static FpState  g_state          = FpState::PRIME;
static uint32_t g_state_entry_ms = 0;
static uint8_t  g_mode           = FUEL_PUMP_DEFAULT_MODE;
static uint8_t  g_last_mode      = FUEL_PUMP_DEFAULT_MODE;
static uint32_t g_last_rpm_ok_ms  = 0;
static uint32_t g_last_coil_ok_ms = 0;
static bool     g_rpm_seen       = false;
static bool     g_coil_seen      = false;

static const char* state_name(FpState s) {
  switch (s) {
    case FpState::PRIME:   return "PRIME";
    case FpState::ARMED:   return "ARMED";
    case FpState::RUNNING: return "RUNNING";
  }
  return "?";
}

static bool gate_rpm_ok(uint32_t now) {
  if (!g_rpm_seen) return false;
  return (now - g_last_rpm_ok_ms) < (uint32_t)FUEL_PUMP_STALL_MS;
}
static bool gate_coil_ok(uint32_t now) {
  if (!g_coil_seen) return false;
  return (now - g_last_coil_ok_ms) < (uint32_t)FUEL_PUMP_STALL_MS;
}

// Returns true if the pump is permitted by the current mode + gate state.
static bool gates_permit(uint32_t now) {
  if (g_mode == FP_MODE_OFF) return true;  // safety disabled, pump forced on
  bool ok = true;
  if (g_mode & 0x01) ok = ok && gate_rpm_ok(now);
  if (g_mode & 0x02) ok = ok && gate_coil_ok(now);
  return ok;
}

static uint8_t gates_ok_bitmap(uint32_t now) {
  uint8_t b = 0;
  if (gate_rpm_ok(now))  b |= 0x01;
  if (gate_coil_ok(now)) b |= 0x02;
  return b;
}

static void emit_relay(bool on) {
  uint8_t mask = (uint8_t)(1u << FUEL_PUMP_RELAY);
  uint8_t d[2] = { mask, on ? mask : (uint8_t)0 };
  bus_tx(CAN_ID_RELAY_CMD, d, 2);
}

static void emit_state(FpReason reason) {
  uint8_t d[4] = {};
  d[0] = (uint8_t)g_state;
  d[1] = g_mode;
  d[2] = (uint8_t)reason;
  d[3] = gates_ok_bitmap(millis());
  bus_tx(CAN_ID_FUEL_PUMP_STATE, d, 4);
}

static void emit_stall_alert() {
  // BUZZER_CMD (0x104): [target_node_id, cmd, arg0]
  // Broadcast target so the switch panel buzzer + Cardputer both react.
  uint8_t d[3] = { CFG_TARGET_BROADCAST, BUZZER_SEQ_FUEL_PUMP_OFF, 0 };
  bus_tx(CAN_ID_BUZZER_CMD, d, 3);
}

static void enter_state(FpState s, FpReason reason) {
  if (s == g_state) return;
  wlog("[fp] %s → %s (reason=%u, mode=%u, gates=%02x)\n",
       state_name(g_state), state_name(s),
       (unsigned)reason, (unsigned)g_mode, (unsigned)gates_ok_bitmap(millis()));
  g_state          = s;
  g_state_entry_ms = millis();
  switch (s) {
    case FpState::PRIME:   emit_relay(true);  break;
    case FpState::ARMED:   emit_relay(false); break;
    case FpState::RUNNING: emit_relay(true);  break;
  }
  emit_state(reason);
  if (reason == FP_REASON_STALL) emit_stall_alert();
}

void fuel_pump_setup() {
  g_state           = FpState::PRIME;
  g_state_entry_ms  = millis();
  g_mode            = FUEL_PUMP_DEFAULT_MODE;
  g_last_mode       = FUEL_PUMP_DEFAULT_MODE;
  g_last_rpm_ok_ms  = 0;
  g_last_coil_ok_ms = 0;
  g_rpm_seen        = false;
  g_coil_seen       = false;
  wlog("[fp] safety armed: mode=%u prime=%lums thresh=%uRPM stall=%lums relay=R%u\n",
       (unsigned)g_mode,
       (unsigned long)FUEL_PUMP_PRIME_MS,
       (unsigned)FUEL_PUMP_RPM_THRESHOLD,
       (unsigned long)FUEL_PUMP_STALL_MS,
       (unsigned)(FUEL_PUMP_RELAY + 1));
  emit_relay(true);  // start prime
  emit_state(FP_REASON_BOOT);
}

bool fuel_pump_safety_enabled() { return g_mode != FP_MODE_OFF; }
uint8_t fuel_pump_mode()         { return g_mode; }

void fuel_pump_set_mode(uint8_t mode) {
  mode &= 0x07;  // RPM | COIL | OIL — clamp to known bits even if caller passes garbage
  if (mode == g_mode) return;
  uint8_t prev = g_mode;
  g_mode = mode;
  wlog("[fp] mode %u → %u\n", (unsigned)prev, (unsigned)g_mode);

  // Restart cycle: if transitioning to OFF, force pump on and freeze; otherwise
  // restart from PRIME so the carb bowl is primed before the next gate check.
  if (g_mode == FP_MODE_OFF) {
    g_state          = FpState::PRIME;  // logical state; effectively held here
    g_state_entry_ms = millis();
    emit_relay(true);
    emit_state(FP_REASON_MODE_CHANGE);
  } else {
    g_state          = FpState::PRIME;
    g_state_entry_ms = millis();
    emit_relay(true);
    emit_state(FP_REASON_MODE_CHANGE);
  }
}

void fuel_pump_set_safety_enabled(bool enabled) {
  // Back-compat: bool API maps to "RPM-only" (the old single-gate behavior).
  fuel_pump_set_mode(enabled ? FP_MODE_RPM : FP_MODE_OFF);
}

void fuel_pump_loop() {
  uint32_t now = millis();

  if (g_mode == FP_MODE_OFF) return;  // mode change handler already emitted relay-on

  switch (g_state) {
    case FpState::PRIME:
      if ((now - g_state_entry_ms) >= (uint32_t)FUEL_PUMP_PRIME_MS) {
        enter_state(FpState::ARMED, FP_REASON_PRIME_DONE);
      }
      break;
    case FpState::ARMED:
      if (gates_permit(now)) {
        enter_state(FpState::RUNNING, FP_REASON_GATE_PASS);
      }
      break;
    case FpState::RUNNING:
      if (!gates_permit(now)) {
        wlog("[fp] stall — gates=%02x mode=%u\n",
             (unsigned)gates_ok_bitmap(now), (unsigned)g_mode);
        enter_state(FpState::ARMED, FP_REASON_STALL);
      }
      break;
  }
}

void fuel_pump_handle_frame(const BusFrame& f) {
  if (f.id == CAN_ID_ENGINE_DATA && f.dlc >= 2) {
    uint16_t rpm = (uint16_t)f.data[0] | ((uint16_t)f.data[1] << 8);
    if (rpm >= FUEL_PUMP_RPM_THRESHOLD) {
      g_last_rpm_ok_ms = millis();
      g_rpm_seen       = true;
    }
    return;
  }

  if (f.id == CAN_ID_IGNITION_DATA && f.dlc >= 3) {
    if (f.data[2] != 0) {  // ign_on
      g_last_coil_ok_ms = millis();
      g_coil_seen       = true;
    }
    return;
  }

  if (f.id == CAN_ID_CONFIG_WRITE && f.dlc >= 5) {
    uint8_t target = f.data[0];
    uint8_t key    = f.data[1];
    if ((target == NODE_ID || target == CFG_TARGET_BROADCAST) &&
        key == CFG_KEY_FUEL_PUMP_SAFETY) {
      fuel_pump_set_mode(f.data[4]);
    }
    return;
  }
}

#endif // ENABLE_FUEL_PUMP_SAFETY
