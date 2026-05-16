// mod_battery.cpp — dual-channel battery voltage ADC and telemetry broadcast.
// Reads VBAT_ADC_PIN (main battery) and optionally VBAT2_ADC_PIN (aux battery)
// via resistor voltage dividers, then broadcasts CAN_ID_TELEMETRY periodically.
//
// Telemetry frame layout (0x300, 8 bytes):
//   [0-1] main battery centivolt, int16 LE (e.g. 1250 = 12.50 V)
//   [2-3] reserved, 0
//   [4-5] aux battery centivolt, int16 LE  (0 when VBAT2_ADC_PIN is not defined)
//   [6-7] flags, reserved

#include <Arduino.h>
#include "node_config.h"

#ifdef ENABLE_BATTERY

#include "can_protocol.h"
#include "bus.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#ifndef VBAT_SAMPLE_MS
#define VBAT_SAMPLE_MS 1000
#endif

static int16_t read_voltage_cv(uint8_t pin, float ratio) {
  int raw = analogRead(pin);
  float vadc = (raw / 4095.0f) * 3.3f;
  return (int16_t)(vadc * ratio * 100.0f);
}

void battery_setup() {
  analogReadResolution(12);
#ifdef VBAT_ADC_PIN
  wlog("[battery] main pin=%u ratio=%.3f\n", VBAT_ADC_PIN, (float)VBAT_DIVIDER_RATIO);
#endif
#ifdef VBAT2_ADC_PIN
  wlog("[battery] aux  pin=%u ratio=%.3f\n", VBAT2_ADC_PIN, (float)VBAT2_DIVIDER_RATIO);
#endif
}

void battery_loop() {
  static uint32_t last_sample = 0;
  uint32_t now = millis();
  if (now - last_sample < VBAT_SAMPLE_MS) return;
  last_sample = now;

  uint8_t d[8] = {};
#ifdef VBAT_ADC_PIN
  pack_i16(&d[0], read_voltage_cv(VBAT_ADC_PIN,  VBAT_DIVIDER_RATIO));
#endif
#ifdef VBAT2_ADC_PIN
  pack_i16(&d[4], read_voltage_cv(VBAT2_ADC_PIN, VBAT2_DIVIDER_RATIO));
#endif
  bus_tx(CAN_ID_TELEMETRY, d, 8);
}

#endif // ENABLE_BATTERY
