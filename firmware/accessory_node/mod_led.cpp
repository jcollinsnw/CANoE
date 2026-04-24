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
static uint8_t g_led_state = 0;  // bitmask: bit 0 = LED 1, etc.

static void apply(uint8_t mask, uint8_t state) {
  g_led_state = (g_led_state & ~mask) | (state & mask);
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    bool on = (g_led_state >> i) & 1;
#if LED_ACTIVE_HIGH
    digitalWrite(LED_PINS[i], on ? HIGH : LOW);
#else
    digitalWrite(LED_PINS[i], on ? LOW : HIGH);
#endif
  }
}

static void send_status() {
  uint8_t d[2] = { NODE_ID, g_led_state };
  bus_tx(CAN_ID_LED_STATUS, d, 2);
}

void led_setup() {
  for (uint8_t i = 0; i < NUM_LEDS; i++) {
    pinMode(LED_PINS[i], OUTPUT);
#if LED_ACTIVE_HIGH
    digitalWrite(LED_PINS[i], LOW);
#else
    digitalWrite(LED_PINS[i], HIGH);
#endif
  }
  wlog("[LED] %u LEDs ready (all off)\n", NUM_LEDS);
}

void led_handle_frame(const BusFrame& f) {
  if (f.id != CAN_ID_LED_CMD || f.dlc < 3) return;
  uint8_t target = f.data[0];
  if (target != NODE_ID && target != CFG_TARGET_BROADCAST) return;
  uint8_t mask  = f.data[1];
  uint8_t state = f.data[2];
  apply(mask, state);
  send_status();
  wlog("[LED] mask=0x%02X state=0x%02X -> bitmap=0x%02X\n", mask, state, g_led_state);
}

#endif // ENABLE_LEDS
