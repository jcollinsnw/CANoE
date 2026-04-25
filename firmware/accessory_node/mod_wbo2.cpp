// mod_wbo2.cpp — wideband O2 sensor reader and CAN broadcaster.
//
// This module reads a 0–5V analog signal from a wideband O2 sensor controller (e.g., LSU 4.9 + controller)
// and broadcasts the lambda or AFR value over CAN. Optionally registers an LCD widget for display.
//
// Config macros (define in node_config.h):
//   WBO2_PIN         — GPIO connected to sensor analog output (must be ADC-capable)
//   WBO2_SAMPLE_MS   — broadcast interval in ms (default 500)
//   WBO2_WIDGET_ROW  — LCD row for the widget (optional)
//   WBO2_WIDGET_COL  — LCD column for the widget (optional)
//   WBO2_WIDGET_WIDTH— number of chars wide (default 8)
//   WBO2_MIN_V       — voltage at minimum reading (default 0.0)
//   WBO2_MAX_V       — voltage at maximum reading (default 5.0)
//   WBO2_MIN_AFR     — AFR at min voltage (default 10.0)
//   WBO2_MAX_AFR     — AFR at max voltage (default 20.0)
//
// CAN protocol:
//   CAN_ID_WBO2_DATA — [afr_lo, afr_hi] (uint16, AFR × 100)
//
// To use: #define ENABLE_WBO2 in your node config and set WBO2_PIN, etc.

#include <Arduino.h>
#include <Preferences.h>
#include "node_config.h"

#ifdef ENABLE_WBO2

#include "can_protocol.h"
#include "bus.h"
#include "mod_lcd.h"

#ifndef WBO2_SAMPLE_MS
#define WBO2_SAMPLE_MS 500
#endif
#ifndef WBO2_WIDGET_WIDTH
#define WBO2_WIDGET_WIDTH 8
#endif
#ifndef WBO2_MIN_V
#define WBO2_MIN_V 0.0f
#endif
#ifndef WBO2_MAX_V
#define WBO2_MAX_V 5.0f
#endif
#ifndef WBO2_MIN_AFR
#define WBO2_MIN_AFR 10.0f
#endif
#ifndef WBO2_MAX_AFR
#define WBO2_MAX_AFR 20.0f
#endif

// Latest AFR value (×100)
static uint16_t g_afr = 0;

// --------------------------------------------------------------
// Sensor — analog read and broadcast
// --------------------------------------------------------------
#ifdef WBO2_PIN

static uint32_t g_last_sample_ms = 0;

static float read_voltage() {
  int raw = analogRead(WBO2_PIN);
  return (float)raw * 3.3f / 4095.0f; // ESP32 ADC: 0–4095 = 0–3.3V (adjust if using 5V ref)
}

static float voltage_to_afr(float v) {
  // Linear map from voltage to AFR
  float afr = WBO2_MIN_AFR + (v - WBO2_MIN_V) * (WBO2_MAX_AFR - WBO2_MIN_AFR) / (WBO2_MAX_V - WBO2_MIN_V);
  if (afr < WBO2_MIN_AFR) afr = WBO2_MIN_AFR;
  if (afr > WBO2_MAX_AFR) afr = WBO2_MAX_AFR;
  return afr;
}

static void sensor_setup() {
  pinMode(WBO2_PIN, INPUT);
  g_last_sample_ms = millis();
}

static void sensor_loop() {
  uint32_t now = millis();
  if (now - g_last_sample_ms < WBO2_SAMPLE_MS) return;
  g_last_sample_ms = now;

  float v = read_voltage();
  float afr = voltage_to_afr(v);
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
