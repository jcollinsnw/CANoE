// Node configuration for the bridge node.
// Connects to an existing WiFi router in STA mode while also running a
// SoftAP and participating on wired CAN. No feature flags — the bridge
// is a pure CAN-bus-to-WiFi/MQTT gateway.
//
// IMPORTANT: Set STA_SSID / STA_PASSWORD before flashing.
// ESP-NOW is intentionally disabled (channel conflict with STA).
//
// Copied to accessory_node/node_config.h by the Makefile before compiling.

#pragma once

#define NODE_NAME           "bridge"
#define NODE_ID             0x05
#define USE_CAN_TRANSCEIVER 1
#define CAN_BUS_SPEED       125   // kbps — must match all other nodes
#define USE_WIFI            1
#define AP_SSID     "REDACTED"
#define AP_PASSWORD "REDACTED"
#define AP_HIDDEN   0

// ---- Home router connection (STA mode) ----
// Set these before flashing. The bridge connects here so it is
// reachable from your home network in addition to its own AP.
#define STA_SSID     "YourHomeNetwork"   // replace before use
#define STA_PASSWORD "YourPassword"      // replace before use

#define NVS_NAMESPACE "bridge"

// ---- Bridge mode ----
// Enables aggregated node discovery UI and /api/nodecaps endpoint.
// Also disables ESP-NOW at boot (bus_init_no_wifi path).
#define BRIDGE_MODE  1

// ---- MQTT (optional) ----
// Fill in MQTT_BROKER and uncomment to enable CAN-frame publishing.
// Requires PubSubClient: arduino-cli lib install "PubSubClient"
//
// #define MQTT_BROKER       "192.168.1.100"   // broker IP or hostname
// #define MQTT_PORT         1883
// #define MQTT_TOPIC_PREFIX "canbus"           // publish: canbus/frames  subscribe: canbus/send
// #define MQTT_CLIENT_ID    "bridge"
// #define MQTT_USER         ""                 // leave empty for anonymous brokers
// #define MQTT_PASS         ""
