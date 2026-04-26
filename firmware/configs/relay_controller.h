// Node configuration for the relay controller ESP32.
// Copied to accessory_node/node_config.h by the Makefile before compiling.

#pragma once

#define NODE_NAME           "relay-ctrl"
#define NODE_ID             0x02
#define USE_CAN_TRANSCEIVER 1
#define CAN_BUS_SPEED       125   // kbps — change all nodes together: 125, 250, or 500
#define USE_WIFI            1
#define AP_SSID     "RelayBus"
#define AP_PASSWORD ""        // change to WPA2 passphrase before field use
#define AP_HIDDEN   0

#define NVS_NAMESPACE       "relayctl"

// ---- Features enabled on this node ----
#define ENABLE_RELAY
// #define ENABLE_RPM

// ---- Relay module ----
#define NUM_RELAYS          6
#define RELAY_ACTIVE_HIGH   true
#define RELAY_PINS_INIT     {16, 17, 18, 19, 21, 22}
#define RELAY_MAX_ON_INIT   {0, 0, 0, 0, 30000, 0}  // relay 5 (horn) = 30 s max

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
