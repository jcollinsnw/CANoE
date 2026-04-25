// mod_buzzer.cpp — passive piezo tone sequencer.
// Uses Arduino tone()/noTone() to generate PWM at the correct frequency.
// Sequences are non-blocking: buzzer_tick() advances one note per call.

#include "node_config.h"

#ifdef ENABLE_BUZZER

#include <Arduino.h>
#include "can_protocol.h"
#include "mod_buzzer.h"

struct Note { uint16_t freq; uint16_t ms; };  // freq=0 → silence gap

static const Note* g_seq     = nullptr;
static uint8_t     g_seq_len = 0;
static uint8_t     g_seq_pos = 0;
static uint32_t    g_note_end = 0;

static void play_seq(const Note* seq, uint8_t len) {
  g_seq     = seq;
  g_seq_len = len;
  g_seq_pos = 0;
  g_note_end = 0;  // fire immediately on next tick
}

void buzzer_setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

void buzzer_tick() {
  if (!g_seq) return;
  uint32_t now = millis();
  if (now < g_note_end) return;

  if (g_seq_pos >= g_seq_len) {
    noTone(BUZZER_PIN);
    g_seq = nullptr;
    return;
  }

  const Note& n = g_seq[g_seq_pos++];
  if (n.freq == 0) noTone(BUZZER_PIN);
  else             tone(BUZZER_PIN, n.freq);
  g_note_end = now + n.ms;
}

// ---------- sound definitions ----------
// Frequencies: E5=659 G5=784 C6=1047 E6=1319 G6=1568

void buzzer_menu_enter() {
  static const Note s[] = { {659,45}, {1047,80} };
  play_seq(s, 2);
}

void buzzer_menu_exit() {
  static const Note s[] = { {1047,45}, {659,80} };
  play_seq(s, 2);
}

void buzzer_menu_scroll() {
  static const Note s[] = { {784,18} };
  play_seq(s, 1);
}

void buzzer_menu_select() {
  static const Note s[] = { {784,35}, {1047,50} };
  play_seq(s, 2);
}

void buzzer_menu_action() {
  static const Note s[] = { {659,30}, {784,30}, {1319,90} };
  play_seq(s, 3);
}

void buzzer_relay_on() {
  static const Note s[] = { {784,40}, {1047,55} };
  play_seq(s, 2);
}

void buzzer_relay_off() {
  static const Note s[] = { {1047,40}, {659,55} };
  play_seq(s, 2);
}

void buzzer_all_off() {
  static const Note s[] = { {1047,30}, {784,30}, {523,80} };
  play_seq(s, 3);
}

void buzzer_handle_frame(const BusFrame& f) {
  if (f.id != CAN_ID_RELAY_CMD || f.dlc < 2) return;
  uint8_t mask = f.data[0], state = f.data[1];
  if (mask == 0x3F && state == 0)   buzzer_all_off();
  else if (state & mask)            buzzer_relay_on();
  else                              buzzer_relay_off();
}

#endif // ENABLE_BUZZER
