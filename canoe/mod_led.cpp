#include "node_config.h"

#ifdef ENABLE_LEDS

#include <Arduino.h>
#include "can_protocol.h"
#include "bus.h"
#include "mod_led.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

static const uint8_t LED_PINS[NUM_LEDS] = LED_PINS_INIT;

// Logical state: bit set = LED "active" (solid on OR flashing).
// Reflects intent, not the instantaneous pin level during a flash cycle.
static uint8_t g_led_state = 0;

// Per-LED flash state. half_ms == 0 means solid mode.
struct FlashState {
  uint16_t half_ms;   // toggle interval; 0 = solid
  bool     phase;     // current output phase (true = on)
  uint32_t next_ms;   // millis() when next toggle is due
};
static FlashState g_flash[NUM_LEDS];

static void led_pin_set(uint8_t i, bool on) {
#if LED_ACTIVE_HIGH
  digitalWrite(LED_PINS[i], on ? HIGH : LOW);
#else
  digitalWrite(LED_PINS[i], on ? LOW  : HIGH);
#endif
}

static void send_status() {
  uint8_t d[2] = { NODE_ID, g_led_state };
  bus_tx(CAN_ID_LED_STATUS, d, 2);
}

void led_setup() {
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    led_pin_set(i, false);
  }
  wlog("[LED] %u LEDs ready\n", NUM_LEDS);
}

// LED_CMD frame handler.
// 3-byte form [target, mask, state]          → solid on/off (backward-compatible).
// 4-byte form [target, mask, state, period]  → period > 0 enables flash;
//   half-period = period × 50 ms (e.g. 0x0A = 500 ms → 1 Hz).
//   LEDs with their mask bit set AND state bit set enter flash mode.
//   LEDs with their mask bit set AND state bit 0 turn off (and flash cleared).
void led_handle_frame(const BusFrame& f) {
  if (f.id != CAN_ID_LED_CMD || f.dlc < 3) return;
  uint8_t target = f.data[0];
  if (target != NODE_ID && target != CFG_TARGET_BROADCAST) return;

  uint8_t  mask    = f.data[1] & ((1u << NUM_LEDS) - 1u);
  uint8_t  state   = f.data[2];
  uint16_t half_ms = (f.dlc >= 4 && f.data[3] > 0) ? (uint16_t)f.data[3] * 50u : 0u;

  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    if (!(mask & (1u << i))) continue;
    bool want_on = (state >> i) & 1u;
    if (!want_on) {
      g_flash[i].half_ms = 0;
      g_led_state &= ~(1u << i);
      led_pin_set(i, false);
    } else if (half_ms == 0) {
      g_flash[i].half_ms = 0;
      g_led_state |= (1u << i);
      led_pin_set(i, true);
    } else {
      g_flash[i] = { half_ms, true, now + half_ms };
      g_led_state |= (1u << i);
      led_pin_set(i, true);
    }
  }
  send_status();
  wlog("[LED] mask=0x%02X state=0x%02X half_ms=%u -> bitmap=0x%02X\n",
       mask, state, half_ms, g_led_state);
}

// Call from loop(). Advances any active flash timers.
void led_tick() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    if (!g_flash[i].half_ms) continue;
    if (now >= g_flash[i].next_ms) {
      g_flash[i].phase   = !g_flash[i].phase;
      g_flash[i].next_ms = now + g_flash[i].half_ms;
      led_pin_set(i, g_flash[i].phase);
    }
  }
}

#endif // ENABLE_LEDS
