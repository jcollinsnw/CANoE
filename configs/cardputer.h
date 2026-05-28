// Node configuration for the M5Stack Cardputer (ESP32-S3).
// Copied to accessory_node/node_config.h by the Makefile before compiling.
//
// The Cardputer acts as a portable CAN bus terminal: keyboard CLI for frame
// injection, TFT display for relay status and frame log, and full dual-
// transport (CAN wire + ESP-NOW) integration via the standard bus.cpp layer.
//
// Requires:
//   - M5Stack board support: arduino-cli core install m5stack:esp32
//   - M5Cardputer library:   arduino-cli lib install "M5Cardputer"
//   - External CAN transceiver (e.g. M5 CAN Unit / TJA1051) on GPIO 1/2

#pragma once
#define NODE_NAME           "cardputer"
#define NODE_ID             0x06
#define USE_CAN_TRANSCEIVER 1
#define CAN_BUS_SPEED       125   // kbps — must match all other nodes
#define USE_WIFI            1
#define NVS_NAMESPACE       "cardputer"

// CAN transceiver on the Cardputer's Grove/expansion port
#define CAN_TX_PIN          GPIO_NUM_1
#define CAN_RX_PIN          GPIO_NUM_2

// ESP-NOW only — no SoftAP, no web server. Saves RAM and avoids running an
// AP on the Cardputer's small antenna alongside the TFT refresh.
#define ESPNOW_ONLY

// ---- WiFi / ESP-NOW credentials ----
#include "secrets.h"

// ---- Features ----
#define ENABLE_M5_CARDPUTER
#define ENABLE_OTA_UPLOAD          // SD-card → HTTP POST OTA client (target node at 192.168.4.1)