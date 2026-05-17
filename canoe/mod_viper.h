#pragma once
#include "node_config.h"

#ifdef ENABLE_VIPER
#include "bus.h"

void viper_setup();
void viper_loop();
void viper_handle_frame(const BusFrame& f);

#endif // ENABLE_VIPER
