#pragma once
#include "node_config.h"

#ifdef ENABLE_DHT22
#include "bus.h"

#ifndef DHT_INTERVAL_MS
#define DHT_INTERVAL_MS 5000  // broadcast every 5 s (sensor min is ~2 s)
#endif

void dht22_setup();
void dht22_loop();

#endif // ENABLE_DHT22
