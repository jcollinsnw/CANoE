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

void bus_init(uint8_t node_id);          // WiFi + ESP-NOW enabled
void bus_init_no_wifi(uint8_t node_id);  // CAN-only; skips ESP-NOW entirely
bool bus_tx(uint32_t id, const uint8_t* data, uint8_t dlc);
bool bus_rx(BusFrame& out);
void bus_tick();

uint8_t bus_node_id();
bool bus_can_healthy();
bool bus_wifi_seen_peer();
void bus_set_wifi_only(bool on);
bool bus_is_wifi_only();

void bus_set_observer(bus_observer_t cb);
