#pragma once
#include "node_config.h"

#ifdef ENABLE_BLUETOOTH
#include "bus.h"

// ── BLE GATT UUIDs ────────────────────────────────────────────────────────
// Must match BLEManager.swift on the iOS side.
#define BLE_SERVICE_UUID "ACC00001-0000-4000-8000-000000000000"
#define BLE_TX_CHAR_UUID "ACC00002-0000-4000-8000-000000000000"  // notify: ESP32 → phone
#define BLE_RX_CHAR_UUID "ACC00003-0000-4000-8000-000000000000"  // write:  phone → ESP32

// Wire format — 11 bytes per CAN frame: [id_lo, id_hi, dlc, d0..d7]
// Unused data bytes beyond dlc are zero-padded.
#define BLE_FRAME_LEN 11

// ── Config macros (optional, define in node_config.h) ─────────────────────
// BLE_DEVICE_NAME   — advertised name (default: NODE_NAME)

void bluetooth_setup();
void bluetooth_loop();
void bluetooth_handle_frame(const BusFrame& f);

#else   // ── stubs when ENABLE_BLUETOOTH is not defined ────────────────────
static inline void bluetooth_setup() {}
static inline void bluetooth_loop() {}
static inline void bluetooth_handle_frame(const BusFrame&) {}
#endif
