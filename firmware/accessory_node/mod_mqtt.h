// mod_mqtt.h
// MQTT publisher/subscriber for the bridge node.
// Active only when MQTT_BROKER is defined in node_config.h.
//
// Topics:
//   {prefix}/frames  — published for every CAN frame received from wire/wifi
//   {prefix}/send    — subscribe here to inject a frame: {"id":N,"data":[b0,b1,...]}

#pragma once
#ifdef MQTT_BROKER
#include "bus.h"
void mqtt_setup();
void mqtt_tick();
void mqtt_handle_frame(const BusFrame& f);
#endif
