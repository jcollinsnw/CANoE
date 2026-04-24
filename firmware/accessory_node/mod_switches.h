#pragma once
#include "node_config.h"

#ifdef ENABLE_SWITCHES
#include "bus.h"

void switches_setup();
void switches_loop();

#endif // ENABLE_SWITCHES
