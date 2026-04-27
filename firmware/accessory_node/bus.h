// bus.h
// Transport abstraction. Each node sends every CAN frame on both
// wired TWAI and ESP-NOW; receivers merge both streams and dedup
// by (node_id, seq). If the wire is cut, WiFi keeps the bus alive.
//
// Usage:
//   bus_init(node_id);          // after setup_can() and webui_init()
//   bus_tx(id, data, dlc);      // send on both transports
//   BusFrame f;
//   while (bus_rx(f)) { ... }   // drain received frames
//   bus_tick();                 // call once per loop()
//
// Observer callback is used by the web UI to log every frame it sees.

#pragma once
#include <stdint.h>

struct BusFrame {
  uint32_t id;
  uint8_t  dlc;
  uint8_t  data[8];
  char     source[8]; // "can" or "wifi"
};

typedef void (*bus_observer_t)(const BusFrame& f, bool outbound);

void bus_init(uint8_t node_id);          // WiFi (AP) already up via webui_init; adds ESP-NOW
void bus_init_no_ap(uint8_t node_id);   // starts WiFi in STA mode on ch6 for ESP-NOW; no AP/HTTP
void bus_init_no_wifi(uint8_t node_id); // CAN-only; WiFi radio stays off
bool bus_tx(uint32_t id, const uint8_t* data, uint8_t dlc);
bool bus_rx(BusFrame& out);
void bus_tick();

uint8_t bus_node_id();
bool    bus_can_healthy();
bool    bus_wifi_seen_peer();
uint8_t bus_peer_count();          // number of distinct ESP-NOW peers seen within the last ~6 s
uint8_t bus_peer_node_bitmap();    // bitmask: bit N set if node 0xN is an active ESP-NOW peer
bool    bus_twai_running(); // current cached TWAI state (updated by bus_twai_check())
bool bus_twai_check();    // run TWAI health check + bus-off recovery; returns true if health changed
enum BusTxMode : uint8_t {
  BUS_TX_CAN_WIFI  = 0,  // both transports (default)
  BUS_TX_WIFI_ONLY = 1,  // skip TWAI TX — test WiFi fallback
  BUS_TX_CAN_ONLY  = 2,  // skip ESP-NOW TX — wired only
};
void     bus_set_tx_mode(BusTxMode mode);
BusTxMode bus_get_tx_mode();

// Legacy wrappers kept for source compat
static inline void bus_set_wifi_only(bool on) { bus_set_tx_mode(on ? BUS_TX_WIFI_ONLY : BUS_TX_CAN_WIFI); }
static inline bool bus_is_wifi_only()         { return bus_get_tx_mode() == BUS_TX_WIFI_ONLY; }

void bus_set_observer(bus_observer_t cb);

// Register CAN-status and WiFi-status LCD widgets. Call once from setup() after lcd_setup().
// No-op when ENABLE_LCD is not defined in node_config.h.
void bus_register_lcd_widgets();
