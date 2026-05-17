#pragma once
#include "bus.h"

void serial_shell_setup();
void serial_shell_tick();
void serial_shell_print(const BusFrame& f);  // call from frame dispatch loop
