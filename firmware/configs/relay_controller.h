// Node configuration for the relay controller ESP32.
// Copied to accessory_node/node_config.h by the Makefile before compiling.

#pragma once

#define NODE_NAME           "relay-ctrl"
#define NODE_ID             0x02
#define USE_CAN_TRANSCEIVER 1
#define CAN_BUS_SPEED       125   // kbps — change all nodes together: 125, 250, or 500
#define USE_WIFI            1
#define AP_SSID     "REDACTED"
#define AP_PASSWORD "REDACTED"
#define AP_HIDDEN   0

#define NVS_NAMESPACE       "relayctl"

// ---- Features enabled on this node ----
#define ENABLE_RELAY
#define ENABLE_RULES
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

// ---- Battery ADC ----
#define VBAT_ADC_PIN        34
#define VBAT_DIVIDER_RATIO  5.545f   // 10k + 2.2k divider; re-tune to your resistors

// ---- Rules engine ----
#define MAX_RULES 8

#define RULES_DEFAULT_INIT {                                                       \
  /* On boot: turn on fuel pump (R1) and choke (R2) */                             \
  RULE(TRIG_BOOT(),         ACT_RELAY_ON(0)),       /* boot → R1 (fuel pump) ON */ \
  RULE(TRIG_BOOT(),         ACT_RELAY_ON(1)),       /* boot → R2 (choke) ON  */    \
  /* Horn safety: R6 on → auto-off after 30 s (edit via web UI Rules tab) */       \
  RULE(TRIG_RELAY_CMD_ON(5), ACT_RELAY_TIMED_OFF(5, 30)),                          \
}
