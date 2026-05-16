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
// #define ENABLE_RPM

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

// ---- Channel capabilities ----
// Advertises which channels this node publishes in response to CHAN_CAP_REQ (0x320).
#define CHAN_CAPS_INIT { \
    CHAN_DEF(CHAN_ID_VBAT,  CAN_ID_TELEMETRY, 0, CHAN_ENC_U16LE|CHAN_SCALE_100,               0, 0x00), \
    CHAN_DEF(CHAN_ID_VBAT2, CAN_ID_TELEMETRY, 4, CHAN_ENC_U16LE|CHAN_SCALE_100|CHAN_SKIP_ZERO, 0, 0x00), \
}

// ---- Rules engine ----
#define MAX_RULES 8

#define RULES_DEFAULT_INIT {                                                       \
  /* On boot: turn on fuel pump (R1) and choke (R2) */                             \
  RULE(TRIG_BOOT(),         ACT_RELAY_ON(0)),       /* boot → R1 (fuel pump) ON */ \
  RULE(TRIG_BOOT(),         ACT_RELAY_ON(1)),       /* boot → R2 (choke) ON  */    \
  /* Horn safety: R6 on → auto-off after 30 s (edit via web UI Rules tab) */       \
  RULE(TRIG_RELAY_CMD_ON(5), ACT_RELAY_TIMED_OFF(5, 30)),                          \
}
