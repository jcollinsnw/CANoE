// mod_fuel_pump.cpp — multi-gate fuel pump safety FSM with on-board
// ignition coil voltage sampling.
//
// On the antique car the master battery switch (chassis ground) keeps the
// relay node powered continuously, even key-out / parked. The OEM pump
// burned out the first time after the boot rule turned R1 on and the pump
// then dead-headed against a closed float valve for hours while the car sat.
//
// This module is the entire safety subsystem for the fuel pump:
//
//   - Samples the ignition coil's + side ADC (IGN_COIL_ADC_PIN), applies
//     on/off hysteresis (IGN_COIL_ON_THRESHOLD_CV / _OFF_THRESHOLD_CV) to
//     derive a clean "is the key in" boolean, and broadcasts
//     CAN_ID_IGNITION_DATA (0x310) periodically + on every edge.
//
//   - Runs a PRIME → ARMED → RUNNING FSM gated on the COIL bit above plus
//     an RPM bit derived from CAN_ID_ENGINE_DATA (0x304) from any source.
//
//   - Owns the fuel pump relay (FUEL_PUMP_RELAY, default R1) — emits a
//     RELAY_CMD on each state transition, and a FUEL_PUMP_STATE (0x311)
//     frame for downstream visibility.
//
//   - On stall (RUNNING → ARMED with the gates going stale, not a
//     user-initiated mode change), broadcasts BUZZER_CMD with
//     BUZZER_SEQ_FUEL_PUMP_OFF so the switch panel and Cardputer both
//     alert audibly.
//
// History: the coil sensing used to live in a separate mod_ignition.cpp
// and reach the FSM via the self-echoed IGNITION_DATA frame. They were
// merged because the only producer of IGNITION_DATA was mod_ignition and
// the only consumer was mod_fuel_pump on the same node — the round trip
// through CAN added complexity without any real decoupling. The frame is
// still broadcast for observability (Cardputer FEED decodes it).
//
// Modes (settable at runtime via CFG_KEY_FUEL_PUMP_SAFETY or rule action):
//   FP_MODE_OFF        — both gates disabled; pump forced ON
//   FP_MODE_RPM        — only RPM gate active
//   FP_MODE_COIL       — only COIL gate active
//   FP_MODE_BOTH       — RPM AND COIL must both pass (default, strictest)
//
// FSM:
//   PRIME (pump ON, FUEL_PUMP_PRIME_MS)        → ARMED (timer elapsed)
//   ARMED (pump OFF, waiting for all enabled gates) → RUNNING (gates pass)
//   RUNNING (pump ON, monitoring gates)        → ARMED (gate stale STALL_MS)
//
// Advisory, not authoritative — the FSM emits a single RELAY_CMD on each
// state transition; manual relay toggles still work between transitions.
//
// Config in node header:
//   ENABLE_FUEL_PUMP_SAFETY      (gate)
//   FUEL_PUMP_RELAY              (relay idx, default 0 / R1)
//   FUEL_PUMP_PRIME_MS           (default 3000)
//   FUEL_PUMP_RPM_THRESHOLD      (default 200)
//   FUEL_PUMP_STALL_MS           (default 2000)
//   FUEL_PUMP_DEFAULT_MODE       (default FP_MODE_BOTH = 3)
// Coil sensing (optional — omit IGN_COIL_ADC_PIN to disable the COIL gate):
//   IGN_COIL_ADC_PIN             (ADC1 input-only GPIO)
//   IGN_COIL_DIVIDER_RATIO       (e.g. 5.545 for 10k+2.2k)
//   IGN_COIL_ON_THRESHOLD_CV     (centivolts; default 600 = 6.00 V)
//   IGN_COIL_OFF_THRESHOLD_CV    (centivolts; default 400 = 4.00 V)
//   IGN_COIL_SAMPLE_MS           (ADC poll, default 200)
//   IGN_COIL_BROADCAST_MS        (heartbeat, default 1000)

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

#ifdef IGN_COIL_ADC_PIN
#ifndef IGN_COIL_SAMPLE_MS
#define IGN_COIL_SAMPLE_MS 200
#endif
#ifndef IGN_COIL_BROADCAST_MS
#define IGN_COIL_BROADCAST_MS 1000
#endif
#ifndef IGN_COIL_ON_THRESHOLD_CV
#define IGN_COIL_ON_THRESHOLD_CV 600
#endif
#ifndef IGN_COIL_OFF_THRESHOLD_CV
#define IGN_COIL_OFF_THRESHOLD_CV 400
#endif
#endif // IGN_COIL_ADC_PIN

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

// ---- FSM state ----
static FpState  g_state           = FpState::PRIME;
static uint32_t g_state_entry_ms  = 0;
static uint8_t  g_mode            = FUEL_PUMP_DEFAULT_MODE;
static uint32_t g_last_rpm_ok_ms  = 0;
static uint32_t g_last_coil_ok_ms = 0;
static bool     g_rpm_seen        = false;
static bool     g_coil_seen       = false;

// ---- Coil sense state ----
#ifdef IGN_COIL_ADC_PIN
static int16_t  g_coil_cv         = 0;
static bool     g_coil_on         = false;
static uint32_t g_coil_last_sample_ms = 0;
static uint32_t g_coil_last_bcast_ms  = 0;
#endif

// --------------------------------------------------------------
// Helpers
// --------------------------------------------------------------
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

// --------------------------------------------------------------
// Coil voltage sampling (folded in from the former mod_ignition)
// --------------------------------------------------------------
#ifdef IGN_COIL_ADC_PIN
static int16_t read_coil_cv() {
  int raw    = analogRead(IGN_COIL_ADC_PIN);
  float vadc = (raw / 4095.0f) * 3.3f;
  return (int16_t)(vadc * (float)IGN_COIL_DIVIDER_RATIO * 100.0f);
}

static void broadcast_coil(uint32_t now) {
  uint8_t d[3] = {};
  pack_i16(&d[0], g_coil_cv);
  d[2] = g_coil_on ? 1 : 0;
  bus_tx(CAN_ID_IGNITION_DATA, d, 3);
  g_coil_last_bcast_ms = now;
}

static void coil_setup() {
  analogReadResolution(12);
  pinMode(IGN_COIL_ADC_PIN, INPUT);
  g_coil_cv        = read_coil_cv();
  g_coil_on        = g_coil_cv >= IGN_COIL_ON_THRESHOLD_CV;
  uint32_t now     = millis();
  g_coil_last_sample_ms = now;
  if (g_coil_on) {
    g_last_coil_ok_ms = now;
    g_coil_seen       = true;
  }
  wlog("[fp/coil] pin=%u ratio=%.3f thresh=%d/%dcv -> coil=%dcv ign=%s\n",
       (unsigned)IGN_COIL_ADC_PIN,
       (float)IGN_COIL_DIVIDER_RATIO,
       (int)IGN_COIL_ON_THRESHOLD_CV,
       (int)IGN_COIL_OFF_THRESHOLD_CV,
       (int)g_coil_cv,
       g_coil_on ? "ON" : "OFF");
  broadcast_coil(now);
}

static void coil_tick(uint32_t now) {
  if (now - g_coil_last_sample_ms < IGN_COIL_SAMPLE_MS) return;
  g_coil_last_sample_ms = now;

  g_coil_cv = read_coil_cv();

  bool was_on = g_coil_on;
  if (g_coil_on) {
    if (g_coil_cv < IGN_COIL_OFF_THRESHOLD_CV) g_coil_on = false;
  } else {
    if (g_coil_cv >= IGN_COIL_ON_THRESHOLD_CV)  g_coil_on = true;
  }

  // While the coil is on, keep refreshing the gate freshness timestamp so
  // gate_coil_ok() stays true until coil_on drops and STALL_MS elapses.
  if (g_coil_on) {
    g_last_coil_ok_ms = now;
    g_coil_seen       = true;
  }

  // Broadcast on edge transitions + periodic heartbeat.
  if (was_on != g_coil_on) {
    wlog("[fp/coil] %s (%dcv)\n", g_coil_on ? "ON" : "OFF", (int)g_coil_cv);
    broadcast_coil(now);
  } else if (now - g_coil_last_bcast_ms >= IGN_COIL_BROADCAST_MS) {
    broadcast_coil(now);
  }
}
#else
static inline void coil_setup() {}
static inline void coil_tick(uint32_t) {}
#endif // IGN_COIL_ADC_PIN

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------
void fuel_pump_setup() {
  g_state           = FpState::PRIME;
  g_state_entry_ms  = millis();
  g_mode            = FUEL_PUMP_DEFAULT_MODE;
  g_last_rpm_ok_ms  = 0;
  g_last_coil_ok_ms = 0;
  g_rpm_seen        = false;
  g_coil_seen       = false;
  coil_setup();  // also seeds g_last_coil_ok_ms if coil is already on at boot
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

#ifdef IGN_COIL_ADC_PIN
bool    fuel_pump_coil_on() { return g_coil_on; }
int16_t fuel_pump_coil_cv() { return g_coil_cv; }
#else
bool    fuel_pump_coil_on() { return false; }
int16_t fuel_pump_coil_cv() { return 0; }
#endif

void fuel_pump_set_mode(uint8_t mode) {
  mode &= 0x07;  // RPM | COIL | OIL — clamp to known bits even if caller passes garbage
  if (mode == g_mode) return;
  uint8_t prev = g_mode;
  g_mode = mode;
  wlog("[fp] mode %u → %u\n", (unsigned)prev, (unsigned)g_mode);

  // Restart cycle: any mode change re-enters PRIME so the carb bowl is
  // refilled before the next gate check. OFF still re-primes; the difference
  // is that gates_permit() then returns true unconditionally so the FSM
  // hands off to ARMED → RUNNING immediately and stays there.
  g_state          = FpState::PRIME;
  g_state_entry_ms = millis();
  emit_relay(true);
  emit_state(FP_REASON_MODE_CHANGE);
}

void fuel_pump_set_safety_enabled(bool enabled) {
  // Back-compat: bool API maps to "RPM-only" (the old single-gate behavior).
  fuel_pump_set_mode(enabled ? FP_MODE_RPM : FP_MODE_OFF);
}

void fuel_pump_loop() {
  uint32_t now = millis();

  coil_tick(now);  // sample ADC and refresh g_last_coil_ok_ms / broadcast IGNITION_DATA

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
