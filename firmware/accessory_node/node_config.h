// Node configuration for the relay controller ESP32.
// Copied to accessory_node/node_config.h by the Makefile before compiling.

#pragma once

#define NODE_NAME           "relay-ctrl"
#define NODE_ID             0x02
#define USE_CAN_TRANSCEIVER 0
#define USE_WIFI            0
#define NVS_NAMESPACE       "relayctl"

// ---- Features enabled on this node ----
#define ENABLE_RELAY

// ---- Relay module ----
#define NUM_RELAYS          6
#define RELAY_ACTIVE_HIGH   true
#define RELAY_PINS_INIT     {16, 17, 18, 19, 21, 22}
#define RELAY_MAX_ON_INIT   {0, 0, 0, 0, 30000, 0}  // relay 5 (horn) = 30 s max

// ---- Battery ADC ----
#define VBAT_ADC_PIN        34
#define VBAT_DIVIDER_RATIO  5.545f   // 10k + 2.2k divider; re-tune to your resistors
