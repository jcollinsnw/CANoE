// mod_ignition.cpp — coil voltage ADC sampler and IGNITION_DATA broadcast.
//
// On the antique car the master battery switch (chassis ground) stays on
// continuously while parked, so the relay node has no "key on / key off"
// signal to drive a fuel-pump cutoff. Reading the ignition coil's + side
// (fed from the key switch through the dash ballast resistor) gives that
// signal: ~9 V in RUN, ~12 V cranking, 0 V key-out.
//
// Voltage divider:  coil+ → 10kΩ → IGN_COIL_ADC_PIN → 2.2kΩ → GND
// Scale factor:     IGN_COIL_DIVIDER_RATIO (5.545 for the 10k/2.2k pair)
//
// Broadcasts IGNITION_DATA (0x310) every IGN_SAMPLE_MS AND immediately
// on every on/off edge so consumers (mod_fuel_pump, Cardputer FEED, the
// web UI) see edge transitions with minimal latency.
//
// Hysteresis: rises above IGN_COIL_ON_THRESHOLD_CV → on; falls below
// IGN_COIL_OFF_THRESHOLD_CV → off. Defeats jittering around the threshold
// when the engine is being cranked.

#include "node_config.h"

#ifdef ENABLE_IGNITION

#include <Arduino.h>
#include "can_protocol.h"
#include "bus.h"
#include "mod_ignition.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#ifndef IGN_COIL_SAMPLE_MS
#define IGN_COIL_SAMPLE_MS 200
#endif
#ifndef IGN_COIL_ON_THRESHOLD_CV
#define IGN_COIL_ON_THRESHOLD_CV 600
#endif
#ifndef IGN_COIL_OFF_THRESHOLD_CV
#define IGN_COIL_OFF_THRESHOLD_CV 400
#endif
#ifndef IGN_COIL_BROADCAST_MS
#define IGN_COIL_BROADCAST_MS 1000
#endif

static int16_t  g_coil_cv         = 0;
static bool     g_ign_on          = false;
static uint32_t g_last_sample_ms  = 0;
static uint32_t g_last_bcast_ms   = 0;

static int16_t read_coil_cv() {
  int raw   = analogRead(IGN_COIL_ADC_PIN);
  float vadc = (raw / 4095.0f) * 3.3f;
  return (int16_t)(vadc * (float)IGN_COIL_DIVIDER_RATIO * 100.0f);
}

static void broadcast_state(uint32_t now) {
  uint8_t d[3] = {};
  pack_i16(&d[0], g_coil_cv);
  d[2] = g_ign_on ? 1 : 0;
  bus_tx(CAN_ID_IGNITION_DATA, d, 3);
  g_last_bcast_ms = now;
}

void ignition_setup() {
  analogReadResolution(12);
  pinMode(IGN_COIL_ADC_PIN, INPUT);
  g_coil_cv        = read_coil_cv();
  g_ign_on         = g_coil_cv >= IGN_COIL_ON_THRESHOLD_CV;
  g_last_sample_ms = millis();
  wlog("[ign] coil pin=%u ratio=%.3f thresh=%d/%dcv -> coil=%dcv ign=%s\n",
       (unsigned)IGN_COIL_ADC_PIN,
       (float)IGN_COIL_DIVIDER_RATIO,
       (int)IGN_COIL_ON_THRESHOLD_CV,
       (int)IGN_COIL_OFF_THRESHOLD_CV,
       (int)g_coil_cv,
       g_ign_on ? "ON" : "OFF");
  broadcast_state(g_last_sample_ms);
}

void ignition_loop() {
  uint32_t now = millis();
  if (now - g_last_sample_ms < IGN_COIL_SAMPLE_MS) return;
  g_last_sample_ms = now;

  g_coil_cv = read_coil_cv();

  bool was_on = g_ign_on;
  if (g_ign_on) {
    if (g_coil_cv < IGN_COIL_OFF_THRESHOLD_CV) g_ign_on = false;
  } else {
    if (g_coil_cv >= IGN_COIL_ON_THRESHOLD_CV)  g_ign_on = true;
  }

  // Broadcast on edge transitions + periodic heartbeat
  if (was_on != g_ign_on) {
    wlog("[ign] coil %s (%dcv)\n", g_ign_on ? "ON" : "OFF", (int)g_coil_cv);
    broadcast_state(now);
  } else if (now - g_last_bcast_ms >= IGN_COIL_BROADCAST_MS) {
    broadcast_state(now);
  }
}

bool    ignition_is_on()  { return g_ign_on; }
int16_t ignition_coil_cv(){ return g_coil_cv; }

#endif // ENABLE_IGNITION
