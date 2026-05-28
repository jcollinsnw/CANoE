// Node configuration for the relay controller ESP32.
// Copied to accessory_node/node_config.h by the Makefile before compiling.

#pragma once

#define NODE_NAME           "relay-ctrl"
#define NODE_ID             0x02
#define USE_CAN_TRANSCEIVER 1
#define CAN_BUS_SPEED       125   // kbps — change all nodes together: 125, 250, or 500
#define USE_WIFI            1
#include "secrets.h"
#define AP_HIDDEN   0

#define NVS_NAMESPACE       "relayctl"

// ---- Features enabled on this node ----
#define ENABLE_RELAY
#define ENABLE_RULES
#define ENABLE_BLUETOOTH
#define ENABLE_RPM

// ---- Relay module ----
#define NUM_RELAYS          6
#define RELAY_ACTIVE_HIGH   true
#define RELAY_PINS_INIT     {16, 19, 17, 22, 18, 21}  // fuse order 1-6: R1=FuelPump R2=Choke R3=Headlights R4-R5=spare R6=Horn
#define RELAY_MAX_ON_INIT   {0, 0, 0, 0, 0, 0}       // hardware watchdog off — horn timeout handled by rules engine

// ---- RPM sensor ----
// PC817C collector → RPM_PIN (with 10kΩ pull-up to 3.3V on that pin).
// GPIO 35 is input-only — no internal pull-up; the external 10kΩ handles it.
#define RPM_PIN         35
#define RPM_CYLINDERS   8     // V8; change to 6 for inline-6, 4 for four-cylinder
#define RPM_SAMPLE_MS   500   // recalculate and broadcast every 500 ms
#define RPM_REDLINE     6500  // also settable at runtime: 400 02 40 00 00 <lo> <hi> 01

// ---- Battery voltage monitoring ----
// GPIO 34 and 36 are input-only ADC1 pins — no pull-up needed, no relay conflicts.
// Wire a resistor voltage divider to each pin (e.g. 10k + 2.2k → ratio 5.545).
// Broadcasts CAN_ID_TELEMETRY (0x300): main bat bytes 0-1, aux bat bytes 4-5.
#define ENABLE_BATTERY
#define VBAT_ADC_PIN         34      // main (primary) battery
#define VBAT_DIVIDER_RATIO   5.545f  // re-tune to your actual resistor values
#define VBAT2_ADC_PIN        36      // aux (secondary) battery
#define VBAT2_DIVIDER_RATIO  5.545f
#define VBAT_SAMPLE_MS       1000

// ---- Fuel pump safety ----
//
// Coil voltage sensing (IGN_COIL_*) is handled inside mod_fuel_pump.cpp so
// these defines belong to the fuel pump safety block, not a separate
// ignition module. Wire: coil + → 10kΩ → IGN_COIL_ADC_PIN → 2.2kΩ → GND
// (5.545:1 divider; 13 V → 2.34 V at ADC, safely below 3.3 V). The off-board
// signal-conditioning PCB documented in assets/coil-interface-schematic.svg
// adds the optocoupler for RPM_PIN plus the divider for IGN_COIL_ADC_PIN
// on a single board that taps the coil between the relay box and the coil
// wiring.
//
// The accessory system runs continuously while the master battery (chassis-
// ground) switch is on, so the relay box is alive even key-out. That is
// what burned out the OEM fuel pump the first time (boot rule turned R1 on,
// pump ran for hours against a closed float valve while the car sat at a
// restaurant). The coil + signal is 0 V key-out, ~9 V key-in-RUN, ~12 V
// during cranking — a clean "is the key actually in" gate.
#define IGN_COIL_ADC_PIN          39      // ADC1, input-only, unused on the relay node
#define IGN_COIL_DIVIDER_RATIO    5.545f
#define IGN_COIL_ON_THRESHOLD_CV  600     // centivolts; >6.00 V → "coil powered"
#define IGN_COIL_OFF_THRESHOLD_CV 400     // centivolts; <4.00 V → "coil off" (hysteresis)
#define IGN_COIL_SAMPLE_MS        200     // ADC poll interval

// Multi-gate FSM. Each gate is an independent freshness check; the pump
// is permitted to run only when ALL ENABLED gates are currently passing:
//   bit 0 (mask 0x01) — RPM gate:  ENGINE_DATA (0x304) rpm >= threshold
//   bit 1 (mask 0x02) — COIL gate: IGNITION_DATA (0x310) coil powered
//   bit 2 (mask 0x04) — reserved for future OIL_PRESSURE gate
//
// FSM:
//   PRIME (pump ON, FUEL_PUMP_PRIME_MS) → ARMED
//   ARMED (pump OFF) → RUNNING when all enabled gates pass
//   RUNNING (pump ON) → ARMED when any enabled gate goes stale for STALL_MS
//
// Defaults to BOTH (RPM AND COIL) — strictest mode. The master switch /
// key-out failure mode that originally burned out the pump is caught by
// either gate alone, and requiring both gives redundancy in case one
// sensor fails (e.g. RPM optocoupler dies → coil gate still gates the
// pump; coil divider opens → RPM gate still does).
//
// Mode is settable at runtime via:
//   - CAN: CONFIG_WRITE [0x02, 0x61, 0, 0, <mode 0-3>, 0, 0, 0]
//   - Rule: ACT_FUEL_PUMP_SAFETY_{DISABLE,RPM_ONLY,COIL_ONLY,BOTH}
// State is NOT persisted; every reboot restores FUEL_PUMP_DEFAULT_MODE.
#define ENABLE_FUEL_PUMP_SAFETY
#define FUEL_PUMP_RELAY         0      // R1
#define FUEL_PUMP_PRIME_MS      3000   // initial prime duration
#define FUEL_PUMP_RPM_THRESHOLD 200    // RPM above which RPM gate passes
#define FUEL_PUMP_STALL_MS      2000   // ms of gate-stale before pump cut
#define FUEL_PUMP_DEFAULT_MODE  3      // 0=off, 1=RPM, 2=COIL, 3=BOTH

// ---- Channel capabilities ----
// Advertises which channels this node publishes in response to CHAN_CAP_REQ (0x320).
#define CHAN_CAPS_INIT { \
    CHAN_DEF(CHAN_ID_VBAT,  CAN_ID_TELEMETRY,    0, CHAN_ENC_U16LE|CHAN_SCALE_100,               0, 0x00), \
    CHAN_DEF(CHAN_ID_VBAT2, CAN_ID_TELEMETRY,    4, CHAN_ENC_U16LE|CHAN_SCALE_100|CHAN_SKIP_ZERO, 0, 0x00), \
    CHAN_DEF(CHAN_ID_RPM,   CAN_ID_ENGINE_DATA,  0, CHAN_ENC_U16LE|CHAN_SCALE_1,                  0, 0x00), \
}

// ---- Rules engine ----
#define MAX_RULES 8

#define RULES_DEFAULT_INIT {                                                       \
  /* R1 (fuel pump) is owned by mod_fuel_pump — see ENABLE_FUEL_PUMP_SAFETY */     \
  /* On boot: turn on choke (R2). */                                               \
  RULE(TRIG_BOOT(),         ACT_RELAY_ON(1)),       /* boot → R2 (choke) ON  */    \
  /* Horn safety: R6 on → auto-off after 30 s (edit via web UI Rules tab) */       \
  RULE(TRIG_RELAY_CMD_ON(5), ACT_RELAY_TIMED_OFF(5, 30)),                          \
}
