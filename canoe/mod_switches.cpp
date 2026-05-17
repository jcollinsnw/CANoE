// mod_switches.cpp — switch/button inputs, debounce, rotary encoder.
//
// This module is purely an input publisher. It detects state changes on
// physical inputs and broadcasts SWITCH_EVENT / ENCODER_EVENT frames via
// bus_tx(). What happens in response to those frames is handled entirely
// by mod_rules (rules engine) or any other node on the bus.
//
// Configured by node_config.h:
//   NUM_SWITCHES, NUM_BUTTONS, INPUT_PINS_INIT
//   ENC_CLK_PIN, ENC_DT_PIN

#include <Arduino.h>
#include "node_config.h"

#ifdef ENABLE_SWITCHES

#include "can_protocol.h"
#include "bus.h"
#include "mod_buzzer.h"
#include "node_state.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#define NUM_INPUTS (NUM_SWITCHES + NUM_BUTTONS)

static const uint8_t INPUT_PINS[NUM_INPUTS] = INPUT_PINS_INIT;

static const uint16_t DEBOUNCE_MS   = 30;
static const uint16_t LONG_PRESS_MS = 600;

// --------------------------------------------------------------
// State
// --------------------------------------------------------------
struct SwitchState {
  uint8_t  raw, stable;
  uint32_t last_change, press_start;
  bool     long_sent;
};

static SwitchState g_sw[NUM_INPUTS];

struct EncoderState { uint8_t last_ab; int8_t pending; };
static EncoderState g_enc;

// --------------------------------------------------------------
// Switch event ACK + retry
// --------------------------------------------------------------
static const uint8_t  SW_MAX_RETRIES = 3;
static const uint16_t SW_RETRY_MS    = 80;
static const uint8_t  SW_PENDING_MAX = 4;
static const uint8_t  SW_ERROR_LED   = 2;   // LED index on this node used as error indicator

struct PendingAck { bool active; uint8_t sw_id, event, retries; uint32_t next_ms; };
static PendingAck g_pending[SW_PENDING_MAX];

// --------------------------------------------------------------
// Outgoing frames
// --------------------------------------------------------------
static void send_sw_event(uint8_t id, uint8_t ev) {
  uint8_t d[2] = { id, ev };
  bus_tx(CAN_ID_SWITCH_EVENT, d, 2);
  wlog("[sw%u] event=%u\n", id, ev);
  // Only register for ACK if the menu is not active — menu actions are local
  // and don't need relay controller confirmation.
  if (g_menu_active) return;
  for (uint8_t i = 0; i < SW_PENDING_MAX; i++) {
    if (!g_pending[i].active) {
      g_pending[i] = { true, id, ev, SW_MAX_RETRIES, millis() + SW_RETRY_MS };
      break;
    }
  }
}

static void send_encoder_event(uint8_t ev, uint8_t count = 1) {
  uint8_t d[2] = { ev, count };
  bus_tx(CAN_ID_ENCODER_EVENT, d, 2);
  wlog("[enc] ev=%u count=%u\n", ev, count);
}

// --------------------------------------------------------------
// Switch polling
// --------------------------------------------------------------
static void poll_switches() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_INPUTS; i++) {
    SwitchState& s = g_sw[i];
    uint8_t raw = digitalRead(INPUT_PINS[i]);
    if (raw != s.raw) { s.raw = raw; s.last_change = now; }
    if ((now - s.last_change) >= DEBOUNCE_MS && raw != s.stable) {
      s.stable = raw;
      if (s.stable == LOW) {
        s.press_start = now; s.long_sent = false;
        send_sw_event(i, SW_PRESS);
      } else {
        send_sw_event(i, SW_RELEASE);
      }
    }
    if (s.stable == LOW && !s.long_sent && (now - s.press_start) >= LONG_PRESS_MS) {
      s.long_sent = true;
      send_sw_event(i, SW_LONG_PRESS);
    }
  }
}

// --------------------------------------------------------------
// Rotary encoder (gray-code, CJMCU-111 EC11) — ±4 sub-steps per detent
// --------------------------------------------------------------
static void poll_encoder() {
  static const int8_t STEP[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0,
  };
  uint8_t ab = (digitalRead(ENC_CLK_PIN) << 1) | digitalRead(ENC_DT_PIN);
  g_enc.pending += STEP[(g_enc.last_ab << 2) | ab];
  g_enc.last_ab = ab;

  if (g_enc.pending >= 4) {
    uint8_t n = (uint8_t)(g_enc.pending / 4); g_enc.pending = 0;
    send_encoder_event(ENC_ROTATE_CW, n);
  } else if (g_enc.pending <= -4) {
    uint8_t n = (uint8_t)((-g_enc.pending) / 4); g_enc.pending = 0;
    send_encoder_event(ENC_ROTATE_CCW, n);
  }
}

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------
void switches_setup() {
  for (uint8_t i = 0; i < NUM_INPUTS; i++) {
    pinMode(INPUT_PINS[i], INPUT_PULLUP);
    g_sw[i].raw         = digitalRead(INPUT_PINS[i]);
    g_sw[i].stable      = g_sw[i].raw;
    g_sw[i].last_change = g_sw[i].press_start = 0;
    g_sw[i].long_sent   = false;
  }

  pinMode(ENC_CLK_PIN, INPUT);
  pinMode(ENC_DT_PIN,  INPUT);
  g_enc.last_ab = (digitalRead(ENC_CLK_PIN) << 1) | digitalRead(ENC_DT_PIN);
  g_enc.pending = 0;

  wlog("[sw] %u inputs ready (%u switches + %u buttons)\n",
       NUM_INPUTS, NUM_SWITCHES, NUM_BUTTONS);
}

void switches_loop() {
  poll_switches();
  poll_encoder();

  // Advance ACK retry timers.
  uint32_t now = millis();
  for (uint8_t i = 0; i < SW_PENDING_MAX; i++) {
    PendingAck& p = g_pending[i];
    if (!p.active || now < p.next_ms) continue;
    if (p.retries > 0) {
      uint8_t d[2] = { p.sw_id, p.event };
      bus_tx(CAN_ID_SWITCH_EVENT, d, 2);
      p.retries--;
      p.next_ms = now + SW_RETRY_MS;
      wlog("[sw] retry sw%u ev%u (%u left)\n", p.sw_id, p.event, p.retries);
    } else {
      wlogln("[sw] ACK timeout — no response after retries");
      buzzer_alert();
      uint8_t led[4] = { NODE_ID, 1u << SW_ERROR_LED, 1u << SW_ERROR_LED, 4 };
      bus_tx(CAN_ID_LED_CMD, led, 4);
      p.active = false;
    }
  }
}

void switches_handle_ack(const BusFrame& f) {
  if (f.id != CAN_ID_SWITCH_ACK || f.dlc < 2) return;
  for (uint8_t i = 0; i < SW_PENDING_MAX; i++) {
    PendingAck& p = g_pending[i];
    if (p.active && p.sw_id == f.data[0] && p.event == f.data[1]) {
      p.active = false;
      wlog("[sw] ACK sw%u ev%u\n", f.data[0], f.data[1]);
      break;
    }
  }
}

void switches_clear_pending() {
  for (uint8_t i = 0; i < SW_PENDING_MAX; i++)
    g_pending[i].active = false;
}

#endif // ENABLE_SWITCHES
