// mod_buzzer.h — passive piezo speaker driver with non-blocking tone sequencer.
// Enable with ENABLE_BUZZER + BUZZER_PIN in node_config.h.

#pragma once
#include "node_config.h"

#ifdef ENABLE_BUZZER

void buzzer_setup();
void buzzer_tick();        // call every loop() iteration

void buzzer_menu_enter();
void buzzer_menu_exit();
void buzzer_menu_scroll();
void buzzer_menu_select();
void buzzer_menu_action();
void buzzer_relay_on();
void buzzer_relay_off();
void buzzer_all_off();

#else

inline void buzzer_setup()       {}
inline void buzzer_tick()        {}
inline void buzzer_menu_enter()  {}
inline void buzzer_menu_exit()   {}
inline void buzzer_menu_scroll() {}
inline void buzzer_menu_select() {}
inline void buzzer_menu_action() {}
inline void buzzer_relay_on()    {}
inline void buzzer_relay_off()   {}
inline void buzzer_all_off()     {}

#endif
