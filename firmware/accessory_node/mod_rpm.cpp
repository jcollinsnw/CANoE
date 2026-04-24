// mod_rpm.cpp — engine RPM sensor and/or LCD bar widget.
//
// This module has three independent sections controlled by config macros:
//
//   Sensor  (requires RPM_PIN defined):
//     Counts falling edges from a PC817C optocoupler wired to the coil (–).
//     Each edge = one coil fire. Broadcasts CAN_ID_ENGINE_DATA every RPM_SAMPLE_MS.
//     Wiring: coil(–) → 270Ω → PC817C pin1; PC817C pin3 → 10kΩ pull-up → 3V3 + RPM_PIN.
//
//   Display widget  (requires ENABLE_LCD + RPM_WIDGET_ROW defined):
//     Registers an LCD bar widget. Subscribes to ENGINE_DATA frames for its data.
//     Works on any node with an LCD — does not need a local coil signal.
//     Bar is 0..RPM_WIDGET_WIDTH filled blocks mapped to 0..redline RPM.
//
//   Frame handler  (always compiled in):
//     Caches the latest RPM from ENGINE_DATA frames (feeds the display widget).
//     Handles CONFIG_WRITE CFG_KEY_RPM_REDLINE to update the redline at runtime.
//
// Config macros (define in node_config.h):
//   RPM_PIN          — GPIO connected to PC817C collector (sensor node)
//   RPM_CYLINDERS    — 8 for V8, 6 for inline-6, 4 for four-cylinder (default 8)
//   RPM_SAMPLE_MS    — broadcast interval in ms (default 500)
//   RPM_WIDGET_ROW   — LCD row for the bar widget
//   RPM_WIDGET_COL   — LCD column for the bar widget
//   RPM_WIDGET_WIDTH — number of characters wide (default 8)
//   RPM_REDLINE      — redline RPM for 100% bar (default 6500); also settable over CAN

#include <Arduino.h>
#include <Preferences.h>
#include "node_config.h"

#ifdef ENABLE_RPM

#include "can_protocol.h"
#include "bus.h"
#include "mod_lcd.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#ifndef RPM_CYLINDERS
#define RPM_CYLINDERS 8
#endif
#ifndef RPM_SAMPLE_MS
#define RPM_SAMPLE_MS 500
#endif
#ifndef RPM_WIDGET_WIDTH
#define RPM_WIDGET_WIDTH 8
#endif
#ifndef RPM_REDLINE
#define RPM_REDLINE 6500
#endif

// Latest RPM — populated by the sensor (if RPM_PIN defined) or by received ENGINE_DATA frames.
static uint16_t g_rpm     = 0;
static uint16_t g_redline = RPM_REDLINE;

// --------------------------------------------------------------
// Sensor — interrupt-based pulse counter (only when RPM_PIN is defined)
// --------------------------------------------------------------
#ifdef RPM_PIN

static volatile uint32_t g_pulse_count = 0;
static uint32_t g_last_sample_ms = 0;

static void IRAM_ATTR rpm_isr() {
  g_pulse_count++;
}

static void sensor_setup() {
  pinMode(RPM_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(RPM_PIN), rpm_isr, FALLING);
  g_last_sample_ms = millis();
  wlogln("[rpm] sensor ready");
}

static void sensor_loop() {
  uint32_t now = millis();
  if (now - g_last_sample_ms < RPM_SAMPLE_MS) return;

  noInterrupts();
  uint32_t count = g_pulse_count;
  g_pulse_count = 0;
  interrupts();

  uint32_t elapsed = now - g_last_sample_ms;
  g_last_sample_ms = now;

  // pulses_per_rev = cylinders / 2 (4-stroke: distributor fires all N cylinders per 2 crank revs)
  const uint8_t ppr = RPM_CYLINDERS / 2;
  g_rpm = (uint16_t)((count * 60000UL) / ((uint32_t)elapsed * ppr));

  uint8_t d[2];
  pack_u16(d, g_rpm);
  bus_tx(CAN_ID_ENGINE_DATA, d, 2);
  wlog("[rpm] %u RPM\n", g_rpm);
}

#endif // RPM_PIN

// --------------------------------------------------------------
// Display widget — bar graph on the LCD (only when position is configured)
// --------------------------------------------------------------
#if defined(ENABLE_LCD) && defined(RPM_WIDGET_ROW)

static void rpm_render(char* buf, uint8_t width) {
  uint32_t fill = (g_redline > 0)
    ? ((uint32_t)g_rpm * width) / g_redline
    : 0;
  if (fill > width) fill = width;
  for (uint8_t i = 0; i < width; i++)
    buf[i] = (i < fill) ? (char)0xFF : ' ';  // 0xFF = solid block on HD44780
}

static void widget_setup() {
  LcdWidget w;
  w.row        = RPM_WIDGET_ROW;
  w.col        = RPM_WIDGET_COL;
  w.width      = RPM_WIDGET_WIDTH;
  w.refresh_ms = RPM_SAMPLE_MS;
  w.render     = rpm_render;
  lcd_register_widget(w);
  wlog("[rpm] widget registered row=%u col=%u width=%u\n",
       RPM_WIDGET_ROW, RPM_WIDGET_COL, RPM_WIDGET_WIDTH);
}

#endif // ENABLE_LCD && RPM_WIDGET_ROW

// --------------------------------------------------------------
// NVS — persist the redline setting
// --------------------------------------------------------------
static void load_redline() {
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  g_redline = p.getUShort("rpm_red", RPM_REDLINE);
  p.end();
}

static void save_redline() {
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putUShort("rpm_red", g_redline);
  p.end();
}

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------
void rpm_setup() {
  load_redline();
#ifdef RPM_PIN
  sensor_setup();
#endif
#if defined(ENABLE_LCD) && defined(RPM_WIDGET_ROW)
  widget_setup();
#endif
}

void rpm_loop() {
#ifdef RPM_PIN
  sensor_loop();
#endif
}

void rpm_handle_frame(const BusFrame& f) {
  // Cache latest RPM from any ENGINE_DATA on the bus (including self-echo from this node).
  if (f.id == CAN_ID_ENGINE_DATA && f.dlc >= 2) {
    g_rpm = unpack_u16(f.data);
    return;
  }

  // Redline config — any node can receive this (display node stores it for the widget;
  // sensor node stores it so it survives a future LCD addition without reflashing).
  if (f.id == CAN_ID_CONFIG_WRITE && f.dlc >= 8 &&
      (f.data[0] == NODE_ID || f.data[0] == CFG_TARGET_BROADCAST) &&
      f.data[1] == CFG_KEY_RPM_REDLINE) {
    uint16_t val = unpack_u16(&f.data[5]);
    if (val > 0) {
      g_redline = val;
      wlog("[rpm] redline → %u RPM\n", g_redline);
      if (f.data[7] & 0x01) save_redline();
      // Echo back a CONFIG_READ_RESP so the sender can confirm.
      uint8_t resp[8] = {};
      resp[0] = NODE_ID;
      resp[1] = CFG_KEY_RPM_REDLINE;
      pack_u16(&resp[5], g_redline);
      bus_tx(CAN_ID_CONFIG_READ_RESP, resp, 8);
    }
  }
}

#endif // ENABLE_RPM
