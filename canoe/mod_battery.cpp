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
#include "mod_error.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#ifndef VBAT_SAMPLE_MS
#define VBAT_SAMPLE_MS 1000
#endif

// Low voltage threshold (centvolts). Default 11.0 V — below this we alert.
// Override in node_config.h if needed.
#ifndef VBAT_LOW_THRESHOLD_CV
#define VBAT_LOW_THRESHOLD_CV 1100
#endif
// Hysteresis: clear alarm when voltage rises above this (default 11.5 V)
#ifndef VBAT_LOW_CLEAR_CV
#define VBAT_LOW_CLEAR_CV 1150
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
  static bool low_voltage_active = false;
  uint32_t now = millis();
  if (now - last_sample < VBAT_SAMPLE_MS) return;
  last_sample = now;

  uint8_t d[8] = {};
  int16_t main_cv = 0;
#ifdef VBAT_ADC_PIN
  main_cv = read_voltage_cv(VBAT_ADC_PIN, VBAT_DIVIDER_RATIO);
  pack_i16(&d[0], main_cv);
#endif
#ifdef VBAT2_ADC_PIN
  pack_i16(&d[4], read_voltage_cv(VBAT2_ADC_PIN, VBAT2_DIVIDER_RATIO));
#endif
  bus_tx(CAN_ID_TELEMETRY, d, 8);

  // Low-voltage alarm with hysteresis.
  // Only check after system has been running 10 s (ignore boot transient / cranking dip).
#ifdef VBAT_ADC_PIN
  if (now > 10000 && main_cv > 0) {
    if (!low_voltage_active && main_cv < VBAT_LOW_THRESHOLD_CV) {
      low_voltage_active = true;
      error_raise_local(ERR_LOW_VOLTAGE, ERR_SEV_CRITICAL, 0);
      wlog("[battery] LOW VOLTAGE: %d.%02d V\n", main_cv / 100, main_cv % 100);
    } else if (low_voltage_active && main_cv > VBAT_LOW_CLEAR_CV) {
      low_voltage_active = false;
      error_clear(ERR_LOW_VOLTAGE);
    }
  }
#endif
}

#endif // ENABLE_BATTERY
