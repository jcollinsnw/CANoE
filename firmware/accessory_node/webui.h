// webui.h
// SoftAP + captive portal + web UI + JSON API for injecting CAN frames
// and watching the bus. Shared identically between nodes — only
// node_name and node_id differ.

#pragma once
#include <stdint.h>
#include "bus.h"

// Call before bus_init(). Starts WiFi AP on a fixed channel so
// ESP-NOW peers can find each other, then boots the HTTP + DNS servers.
void webui_init(const char* node_name, uint8_t node_id);

// Call from loop() — services HTTP client requests and DNS.
void webui_tick();

// Invoked by bus.cpp for every frame we send or receive.
void webui_observe(const BusFrame& f, bool outbound);

// Called from the frame dispatch loop for incoming NODE_CAP (0x0F2) frames.
// Updates the internal node capability cache served by /api/nodecaps.
void webui_handle_node_cap(const BusFrame& f);

// Call once in setup() BEFORE any Serial.print calls to capture
// serial output into the web UI.  Replaces Serial with a tee
// that writes to both the UART and an in-memory ring buffer.
void webui_serial_tee_install();

// Printf-style log to both Serial and the web UI serial buffer.
// Use wlog()/wlogln() instead of Serial.printf()/Serial.println()
// for messages you want visible in the Serial tab.
int wlog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void wlogln(const char* msg);
