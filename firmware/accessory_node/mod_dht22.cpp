// mod_dht22.cpp — AM2302 (DHT22) temperature/humidity sensor, bit-bang protocol.
// Broadcasts CAN_ID_ENV_DATA every DHT_INTERVAL_MS milliseconds.

#include <Arduino.h>
#include "node_config.h"

#ifdef ENABLE_DHT22

#include "mod_dht22.h"
#include "can_protocol.h"
#include "bus.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

static int16_t  g_temp_d1 = 0;    // last reading, 0.1 °C signed
static uint16_t g_humi_d1 = 0;    // last reading, 0.1 %RH

// Read 40 bits from the AM2302. Returns true on success.
static bool dht_read(int16_t& temp_d1, uint16_t& humi_d1) {
  // Host pulls low ≥1 ms to wake sensor
  pinMode(DHT_PIN, OUTPUT);
  digitalWrite(DHT_PIN, LOW);
  delay(2);
  digitalWrite(DHT_PIN, HIGH);
  delayMicroseconds(30);
  pinMode(DHT_PIN, INPUT_PULLUP);

  uint32_t t0 = micros();
  while (digitalRead(DHT_PIN) == HIGH) { if (micros() - t0 > 200) return false; }
  t0 = micros();
  while (digitalRead(DHT_PIN) == LOW)  { if (micros() - t0 > 200) return false; }
  t0 = micros();
  while (digitalRead(DHT_PIN) == HIGH) { if (micros() - t0 > 200) return false; }

  // Read 40 bits: each starts with ~50 µs low; 26-28 µs high = 0, 70 µs high = 1
  uint8_t data[5] = {};
  for (int i = 0; i < 40; i++) {
    t0 = micros();
    while (digitalRead(DHT_PIN) == LOW)  { if (micros() - t0 > 100) return false; }
    t0 = micros();
    while (digitalRead(DHT_PIN) == HIGH) { if (micros() - t0 > 100) return false; }
    if ((micros() - t0) > 40) data[i / 8] |= (1 << (7 - (i % 8)));
  }

  // Checksum
  if (((uint8_t)(data[0] + data[1] + data[2] + data[3])) != data[4]) return false;

  humi_d1 = ((uint16_t)data[0] << 8) | data[1];
  uint16_t t_raw = ((uint16_t)data[2] << 8) | data[3];
  temp_d1 = (t_raw & 0x8000) ? -(int16_t)(t_raw & 0x7FFF) : (int16_t)t_raw;
  return true;
}

void dht22_setup() {}  // no hardware init needed; polling starts in loop

void dht22_loop() {
  static uint32_t last_read = 0;
  uint32_t now = millis();
  if (now - last_read < DHT_INTERVAL_MS) return;
  last_read = now;

  int16_t t; uint16_t h;
  if (dht_read(t, h)) {
    g_temp_d1 = t; g_humi_d1 = h;
    uint8_t d[4];
    pack_i16(&d[0], g_temp_d1);
    pack_u16(&d[2], g_humi_d1);
    bus_tx(CAN_ID_ENV_DATA, d, 4);
    wlog("[dht] %.1f C  %.1f %%\n", t / 10.0f, h / 10.0f);
  } else {
    wlogln("[dht] read failed");
  }
}

#endif // ENABLE_DHT22
