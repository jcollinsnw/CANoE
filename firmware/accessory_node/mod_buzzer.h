// mod_buzzer.h — passive piezo speaker driver with non-blocking tone sequencer.
// Enable with ENABLE_BUZZER + BUZZER_PIN in node_config.h.

#pragma once
#include "node_config.h"
#include "bus.h"

#ifdef ENABLE_BUZZER

void buzzer_setup();
void buzzer_tick();              // call every loop() iteration
void buzzer_handle_frame(const BusFrame& f);  // reacts to RELAY_CMD frames

void buzzer_menu_enter();
void buzzer_menu_exit();
void buzzer_menu_scroll();
void buzzer_menu_select();
void buzzer_menu_action();
void buzzer_relay_on();
void buzzer_relay_off();
void buzzer_all_off();

void buzzer_startup();              // boot-complete jingle (call after WiFi + CAN ready)
void buzzer_can_up();               // CAN bus link restored
void buzzer_can_down();             // CAN bus link lost
void buzzer_peer_count(uint8_t n);  // play pitch for n active peers; queued so simultaneous
                                    // connects stack up rather than cutting each other off

void buzzer_set_muted(bool muted);  // silence all output without stopping sequences
bool buzzer_is_muted();

#else

inline void buzzer_setup()                          {}
inline void buzzer_tick()                           {}
inline void buzzer_handle_frame(const BusFrame&)    {}
inline void buzzer_menu_enter()                     {}
inline void buzzer_menu_exit()                      {}
inline void buzzer_menu_scroll()                    {}
inline void buzzer_menu_select()                    {}
inline void buzzer_menu_action()                    {}
inline void buzzer_relay_on()                       {}
inline void buzzer_relay_off()                      {}
inline void buzzer_all_off()                        {}
inline void buzzer_startup()                        {}
inline void buzzer_can_up()                         {}
inline void buzzer_can_down()                       {}
inline void buzzer_peer_count(uint8_t)              {}
inline void buzzer_set_muted(bool)                  {}
inline bool buzzer_is_muted()                       { return false; }

#endif
