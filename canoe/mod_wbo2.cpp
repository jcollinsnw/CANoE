// mod_wbo2.cpp — wideband O2 sensor reader and CAN broadcaster.
//
// Reads the 0–5V analog output of an LSU 4.9 wideband O2 controller (any brand),
// converts to AFR via a configurable linear map, and broadcasts WBO2_DATA over CAN.
// Optionally registers an LCD widget for display.
//
// The controller's 0–5V output must be divided to 0–2.5V before the ADC pin.
// Use equal-value resistors (e.g. 100 kΩ + 100 kΩ) as a 2:1 divider.
// WBO2_MIN_V / WBO2_MAX_V are the post-divider voltages at the ADC pin.
//
// Config macros (define in node_config.h):
//   WBO2_PIN          — ADC1 GPIO (GPIOs 32–39). ADC2 is unavailable while WiFi is active.
//   WBO2_SAMPLE_MS    — broadcast interval in ms (default 500)
//   WBO2_OVERSAMPLE   — ADC samples averaged per reading (default 16; higher = less noise)
//   WBO2_ADC_VREF     — ADC full-scale voltage in V (default 3.3f; use 3.9f with ADC_11db)
//   WBO2_EMA_ALPHA    — exponential moving average factor (0.0–1.0); omit to disable.
//                       0.2 = heavy smoothing (~400 ms TC at 10 Hz), 0.5 = light smoothing.
//   WBO2_WIDGET_ROW   — LCD row for the widget (optional)
//   WBO2_WIDGET_COL   — LCD column for the widget (optional)
//   WBO2_WIDGET_WIDTH — number of chars wide (default 8)
//   WBO2_MIN_V        — post-divider voltage at WBO2_MIN_AFR (default 0.0)
//   WBO2_MAX_V        — post-divider voltage at WBO2_MAX_AFR (default 2.5)
//   WBO2_MIN_AFR      — AFR at WBO2_MIN_V (default 10.0)
//   WBO2_MAX_AFR      — AFR at WBO2_MAX_V (default 20.0)
//
// CAN protocol:
//   CAN_ID_WBO2_DATA — [afr_lo, afr_hi] (uint16, AFR × 100)
//
// Calibration varies by controller brand. Set WBO2_MIN/MAX_AFR to match your
// controller's output spec. Verify at stoich (14.7 AFR) against a known reference.
//
// To use: #define ENABLE_WBO2 in your node config and set WBO2_PIN, etc.

#include <Arduino.h>
#include <Preferences.h>
#include "node_config.h"

#ifdef ENABLE_WBO2

#include "can_protocol.h"
#include "bus.h"
#include "mod_lcd.h"
#include "mod_error.h"

#ifndef WBO2_SAMPLE_MS
#define WBO2_SAMPLE_MS 500
#endif
#ifndef WBO2_OVERSAMPLE
#define WBO2_OVERSAMPLE 16
#endif
#ifndef WBO2_ADC_VREF
#define WBO2_ADC_VREF 3.3f
#endif
#ifndef WBO2_WIDGET_WIDTH
#define WBO2_WIDGET_WIDTH 8
#endif
#ifndef WBO2_MIN_V
#define WBO2_MIN_V 0.0f
#endif
#ifndef WBO2_MAX_V
#define WBO2_MAX_V 2.5f
#endif
#ifndef WBO2_MIN_AFR
#define WBO2_MIN_AFR 10.0f
#endif
#ifndef WBO2_MAX_AFR
#define WBO2_MAX_AFR 20.0f
#endif

// Latest AFR value (×100)
static uint16_t g_afr = 0;

#ifdef WBO2_EMA_ALPHA
static float g_afr_ema = 0.0f;
#endif

// --------------------------------------------------------------
// Sensor — analog read and broadcast
// --------------------------------------------------------------
#ifdef WBO2_PIN

static uint32_t g_last_sample_ms = 0;

static float read_voltage() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < WBO2_OVERSAMPLE; i++)
    sum += analogRead(WBO2_PIN);
  return (float)(sum / WBO2_OVERSAMPLE) * WBO2_ADC_VREF / 4095.0f;
}

static float voltage_to_afr(float v) {
  float afr = WBO2_MIN_AFR + (v - WBO2_MIN_V) * (WBO2_MAX_AFR - WBO2_MIN_AFR) / (WBO2_MAX_V - WBO2_MIN_V);
  if (afr < WBO2_MIN_AFR) afr = WBO2_MIN_AFR;
  if (afr > WBO2_MAX_AFR) afr = WBO2_MAX_AFR;
  return afr;
}

static void sensor_setup() {
  pinMode(WBO2_PIN, INPUT);
  // 11 dB attenuation extends ADC range to ~3.9 V, needed for post-divider voltages above ~1.1 V.
#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  analogSetAttenuation(WBO2_PIN, ADC_11db);
#else
  analogSetPinAttenuation(WBO2_PIN, ADC_11db);
#endif
  g_last_sample_ms = millis();
}

static void sensor_loop() {
  uint32_t now = millis();
  if (now - g_last_sample_ms < WBO2_SAMPLE_MS) return;
  g_last_sample_ms = now;

  float v = read_voltage();

  // Sensor fault detection: voltage near 0 (disconnected) or near max (shorted).
  // Threshold: < 0.02V or within 0.05V of ADC Vref.
  static bool fault_active = false;
  if (v < 0.02f || v > (WBO2_ADC_VREF - 0.05f)) {
    if (!fault_active) {
      fault_active = true;
      uint8_t raw_byte = (uint8_t)(v * 100.0f);  // rough voltage for diagnostics
      error_raise_local(ERR_WBO2_SENSOR_FAULT, ERR_SEV_CRITICAL, raw_byte);
    }
    return;  // Don't broadcast a false reading
  } else if (fault_active) {
    fault_active = false;
    error_clear(ERR_WBO2_SENSOR_FAULT);
  }

  float afr = voltage_to_afr(v);
#ifdef WBO2_EMA_ALPHA
  if (g_afr_ema == 0.0f) g_afr_ema = afr;
  g_afr_ema = WBO2_EMA_ALPHA * afr + (1.0f - WBO2_EMA_ALPHA) * g_afr_ema;
  afr = g_afr_ema;
#endif
  g_afr = (uint16_t)(afr * 100.0f + 0.5f);

  uint8_t d[2];
  d[0] = g_afr & 0xFF;
  d[1] = g_afr >> 8;
  bus_tx(CAN_ID_WBO2_DATA, d, 2);
}

#endif // WBO2_PIN

// --------------------------------------------------------------
// Display widget — LCD (optional)
// --------------------------------------------------------------
#if defined(ENABLE_LCD) && defined(WBO2_WIDGET_ROW)

static void wbo2_render(char* buf, uint8_t width) {
  float afr = g_afr / 100.0f;
  snprintf(buf, width + 1, "%.*f", (width > 5 ? 2 : 1), afr);
}

static void widget_setup() {
  LcdWidget w;
  w.row        = WBO2_WIDGET_ROW;
  w.col        = WBO2_WIDGET_COL;
  w.width      = WBO2_WIDGET_WIDTH;
  w.refresh_ms = WBO2_SAMPLE_MS;
  w.render     = wbo2_render;
  lcd_register_widget(w);
}

#endif // ENABLE_LCD && WBO2_WIDGET_ROW

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------
void wbo2_setup() {
#ifdef WBO2_PIN
  sensor_setup();
#endif
#if defined(ENABLE_LCD) && defined(WBO2_WIDGET_ROW)
  widget_setup();
#endif
}

void wbo2_loop() {
#ifdef WBO2_PIN
  sensor_loop();
#endif
}

void wbo2_handle_frame(const BusFrame& f) {
  // Cache latest AFR from any WBO2_DATA on the bus
  if (f.id == CAN_ID_WBO2_DATA && f.dlc >= 2) {
    g_afr = f.data[0] | (f.data[1] << 8);
    return;
  }
}

#endif // ENABLE_WBO2
