// mod_buzzer.cpp — passive piezo tone sequencer.
// Uses Arduino tone()/noTone() to generate PWM at the correct frequency.
// Sequences are non-blocking: buzzer_tick() advances one note per call.

#include "node_config.h"

#ifdef ENABLE_BUZZER

#include <Arduino.h>
#include "can_protocol.h"
#include "mod_buzzer.h"

struct Note     { uint16_t freq; uint16_t ms; };  // freq=0 → silence gap
struct SeqEntry { const Note* seq; uint8_t len; };

static const Note* g_seq     = nullptr;
static uint8_t     g_seq_len = 0;
static uint8_t     g_seq_pos = 0;
static uint32_t    g_note_end = 0;
static bool        g_muted   = false;

// Queue for sequences that should play back-to-back without interrupting each other.
#define BZ_QUEUE_SIZE 8
static SeqEntry g_queue[BZ_QUEUE_SIZE];
static uint8_t  g_q_head = 0, g_q_tail = 0;

// Start a sequence immediately with no queue side-effects.
static void start_seq(const Note* seq, uint8_t len) {
  g_seq = seq; g_seq_len = len; g_seq_pos = 0; g_note_end = 0;
}

// Interrupt whatever is playing and clear the queue (user-initiated sounds).
static void play_seq(const Note* seq, uint8_t len) {
  g_q_head = g_q_tail = 0;
  start_seq(seq, len);
}

// Add to queue; if nothing is playing, start immediately (ambient status sounds).
static void enqueue_seq(const Note* seq, uint8_t len) {
  if (!g_seq && g_q_head == g_q_tail) { start_seq(seq, len); return; }
  uint8_t next = (g_q_head + 1) % BZ_QUEUE_SIZE;
  if (next != g_q_tail) { g_queue[g_q_head] = {seq, len}; g_q_head = next; }
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
    if (g_q_head != g_q_tail) {
      SeqEntry e = g_queue[g_q_tail];
      g_q_tail = (g_q_tail + 1) % BZ_QUEUE_SIZE;
      start_seq(e.seq, e.len);
    }
    return;
  }

  const Note& n = g_seq[g_seq_pos++];
  if (n.freq == 0 || g_muted) noTone(BUZZER_PIN);
  else                         tone(BUZZER_PIN, n.freq);
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

void buzzer_startup() {
  // Rising C-major arpeggio: C5 E5 G5 C6 E6 — boot complete jingle
  static const Note s[] = {
    {523,70}, {659,70}, {784,70}, {1047,70}, {1319,180}
  };
  play_seq(s, 5);
}

void buzzer_can_up() {
  // Three rising staccato tones — link restored
  static const Note s[] = { {784,40},{0,20},{1047,40},{0,20},{1319,90} };
  play_seq(s, 5);
}

void buzzer_can_down() {
  // Two low warning pulses — link lost
  static const Note s[] = { {523,60},{0,30},{523,120} };
  play_seq(s, 3);
}

// Pitch ladder: one note per active peer count, queued so simultaneous
// connects play in sequence rather than cutting each other off.
// G4=392  C5=523  E5=659  G5=784  C6=1047  E6=1319
// count:  0       1       2       3        4        5+
static const Note PEER_TONES[][2] = {
  { {392, 150}, {0, 50} },  // 0 — low note, longer (all gone)
  { {523,  80}, {0, 40} },  // 1
  { {659,  80}, {0, 40} },  // 2
  { {784,  80}, {0, 40} },  // 3
  { {1047, 80}, {0, 40} },  // 4
  { {1319, 80}, {0, 40} },  // 5+
};

void buzzer_peer_count(uint8_t n) {
  uint8_t idx = (n < 6) ? n : 5;
  enqueue_seq(PEER_TONES[idx], 2);
}

void buzzer_alert() {
  // Three urgent high-pitched pulses — CAN error or system warning.
  // Higher pitch than buzzer_can_down() so it's clearly distinct.
  static const Note s[] = { {1047,60},{0,30},{1047,60},{0,30},{1047,150} };
  play_seq(s, 5);
}

void buzzer_wifi_connect() {
  // Ascending two-note chime: A5 → C6
  static const Note s[] = { {880, 80}, {1047, 130} };
  enqueue_seq(s, 2);
}

void buzzer_wifi_disconnect() {
  // Descending two-note chime: C6 → A5
  static const Note s[] = { {1047, 80}, {880, 130} };
  enqueue_seq(s, 2);
}

void buzzer_set_muted(bool muted) { g_muted = muted; if (muted) noTone(BUZZER_PIN); }
bool buzzer_is_muted()            { return g_muted; }

void buzzer_handle_frame(const BusFrame& f) {
  if (f.id != CAN_ID_RELAY_CMD || f.dlc < 2) return;
  uint8_t mask = f.data[0], state = f.data[1];
  if (mask == 0x3F && state == 0)   buzzer_all_off();
  else if (state & mask)            buzzer_relay_on();
  else                              buzzer_relay_off();
}

#endif // ENABLE_BUZZER
