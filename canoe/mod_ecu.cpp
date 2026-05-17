// mod_ecu.cpp — dual-mode fuel controller for a carbureted V8.
//
// Mode 0 — closed-loop carb: drives an air-bleed solenoid at 12 Hz PWM (LEDC).
//   Higher duty = more air bleed = leaner mixture.  Neutral = 50% duty.
//   PI controller adjusts duty based on WBO2 AFR error.
//
// Mode 1 — TBI injection: fires dual injectors once per crankshaft revolution.
//   Pulse width = base_pw × VE(rpm,map)/100 × IAT_density × CLT_enrich × STFT_trim.
//   esp_timer_start_once closes the injectors to microsecond accuracy.
//
// All sensors must be on ADC1 pins (ADC2 is unavailable when WiFi is active).
// Required config macros — see firmware/configs/ecu_node.h for full documentation.

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include "esp_timer.h"
#include "node_config.h"

#ifdef ENABLE_ECU

#include "can_protocol.h"
#include "bus.h"

// v3.x changed ledcWrite to take a pin instead of a channel
#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#  define CARB_PWM_WRITE(duty)  ledcWrite(ECU_CARB_SOLENOID_PIN, (duty))
#else
#  define CARB_PWM_WRITE(duty)  ledcWrite(ECU_CARB_LEDC_CH, (duty))
#endif

// ---------------------------------------------------------------
// Shared sensor state (updated by sensor_sample(), read by both loops)
// ---------------------------------------------------------------
static float g_map_kpa = 100.0f;
static float g_tps_pct = 0.0f;
static float g_clt_c   = 80.0f;
static float g_iat_c   = 25.0f;

// Cached from CAN frames
static float    g_afr = 0.0f;    // 0 = no valid reading yet
static uint16_t g_rpm = 0;

// Persistent config
static uint8_t  g_mode;
static float    g_target_afr;
static uint32_t g_base_pw_us;    // computed at startup, optionally overridden by NVS

// Runtime state
static bool     g_fuel_cut = false;
static float    g_integral = 0.0f;   // carb PI integral accumulator
static float    g_carb_duty = 50.0f; // current carb solenoid duty %
static float    g_stft = 0.0f;       // inject STFT correction %
static uint32_t g_pw_us = 0;         // current computed inject pulse width

// esp_timer handle for injector pulse-off
static esp_timer_handle_t g_inj_off_timer = nullptr;

// Last injector fire timestamp (microseconds) — tracks revolution timing in inject mode
static uint32_t g_last_inj_us = 0;

// ---------------------------------------------------------------
// 351 Windsor VE table — 8 RPM × 6 MAP bins, values in %
//
//  RPM bins:  500  1000  1500  2000  2500  3000  4000  5500
//  MAP bins:   20    40    60    75    90   100 kPa
//
// Peak VE (93%) at 3000 RPM / 90–100 kPa matches 351W torque peak.
// Low-load (20–40 kPa) cells reflect real vacuum conditions at idle/part-throttle.
// ---------------------------------------------------------------
static const uint8_t VE_TABLE[8][6] = {
  //  20   40   60   75   90  100 kPa
  {   32,  40,  50,  57,  63,  65 },  //  500 RPM
  {   35,  48,  62,  72,  80,  84 },  // 1000 RPM
  {   38,  55,  68,  78,  86,  90 },  // 1500 RPM
  {   40,  58,  72,  82,  89,  92 },  // 2000 RPM
  {   42,  60,  75,  85,  91,  93 },  // 2500 RPM
  {   44,  62,  77,  87,  92,  93 },  // 3000 RPM  ← torque peak
  {   45,  62,  77,  87,  91,  92 },  // 4000 RPM
  {   40,  55,  70,  82,  86,  87 },  // 5500 RPM
};
static const uint16_t VE_RPM_BINS[8] = { 500, 1000, 1500, 2000, 2500, 3000, 4000, 5500 };
static const uint8_t  VE_MAP_BINS[6] = {  20,   40,   60,   75,   90,  100 };

// CLT enrichment table — cold engine needs extra fuel
static const int8_t  CLT_TEMP[8] = { -20, -10,  0, 20, 40, 60, 80, 100 };
static const uint8_t CLT_ENR[8]  = { 160, 148, 135, 118, 108, 103, 100, 100 };

// ---------------------------------------------------------------
// Sensor reads
// ---------------------------------------------------------------
static float read_map_kpa() {
  int raw = analogRead(ECU_MAP_PIN);
  float vadc = (float)raw * 3.3f / 4095.0f;
  float vsensor = vadc * 2.0f;  // reconstruct sensor output (÷2 voltage divider)
  float kpa = (vsensor - 0.2f) * 250.0f / (4.9f - 0.2f);
  if (kpa < 0.0f)   kpa = 0.0f;
  if (kpa > 250.0f) kpa = 250.0f;
  return kpa;
}

static float read_tps_pct() {
  int raw = analogRead(ECU_TPS_PIN);
  float mv = (float)raw * 3300.0f / 4095.0f;
  float pct = (mv - ECU_TPS_CLOSED_MV) * 100.0f
              / (float)(ECU_TPS_OPEN_MV - ECU_TPS_CLOSED_MV);
  if (pct < 0.0f)   pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return pct;
}

static float read_ntc_c(uint8_t pin) {
  int raw = analogRead(pin);
  if (raw <= 0) return -40.0f;
  float vpin = (float)raw * 3.3f / 4095.0f;
  float denom = 3.3f - vpin;
  if (denom < 0.001f) return 150.0f;
  float r_ntc = (float)ECU_NTC_PULLUP_OHMS * vpin / denom;
  if (r_ntc <= 0.0f) return 150.0f;
  const float t0_k = 298.15f;
  float t_k = 1.0f / (logf(r_ntc / (float)ECU_NTC_R0) / (float)ECU_NTC_BETA + 1.0f / t0_k);
  return t_k - 273.15f;
}

static void sensor_sample() {
  g_map_kpa = read_map_kpa();
  g_tps_pct = read_tps_pct();
  g_clt_c   = read_ntc_c(ECU_CLT_PIN);
  g_iat_c   = read_ntc_c(ECU_IAT_PIN);
}

// ---------------------------------------------------------------
// VE table — bilinear interpolation
// ---------------------------------------------------------------
static float lookup_ve(uint16_t rpm, float map_kpa) {
  // Find RPM bracket
  uint8_t ri = 7;
  for (uint8_t i = 0; i < 7; i++) {
    if (rpm < VE_RPM_BINS[i + 1]) { ri = i; break; }
  }
  float rf = (ri < 7)
    ? (float)(rpm - VE_RPM_BINS[ri]) / (float)(VE_RPM_BINS[ri + 1] - VE_RPM_BINS[ri])
    : 0.0f;
  uint8_t ri2 = (ri < 7) ? ri + 1 : ri;

  // Find MAP bracket
  uint8_t mi = 5;
  for (uint8_t i = 0; i < 5; i++) {
    if (map_kpa < (float)VE_MAP_BINS[i + 1]) { mi = i; break; }
  }
  float mf = (mi < 5)
    ? (map_kpa - VE_MAP_BINS[mi]) / (float)(VE_MAP_BINS[mi + 1] - VE_MAP_BINS[mi])
    : 0.0f;
  uint8_t mi2 = (mi < 5) ? mi + 1 : mi;

  return VE_TABLE[ri ][mi ] * (1 - rf) * (1 - mf)
       + VE_TABLE[ri2][mi ] * rf       * (1 - mf)
       + VE_TABLE[ri ][mi2] * (1 - rf) * mf
       + VE_TABLE[ri2][mi2] * rf       * mf;
}

// ---------------------------------------------------------------
// CLT enrichment — cold-engine fuel correction
// ---------------------------------------------------------------
static float clt_enrichment(float clt_c) {
  if (clt_c <= CLT_TEMP[0]) return CLT_ENR[0] / 100.0f;
  if (clt_c >= CLT_TEMP[7]) return 1.0f;
  for (uint8_t i = 0; i < 7; i++) {
    if (clt_c < CLT_TEMP[i + 1]) {
      float frac = (clt_c - CLT_TEMP[i]) / (float)(CLT_TEMP[i + 1] - CLT_TEMP[i]);
      float enr = CLT_ENR[i] + frac * (int8_t)(CLT_ENR[i + 1] - CLT_ENR[i]);
      return enr / 100.0f;
    }
  }
  return 1.0f;
}

// ---------------------------------------------------------------
// Inject pulse width calculation
// ---------------------------------------------------------------
static uint32_t calc_pulse_width() {
  float ve       = lookup_ve(g_rpm, g_map_kpa);
  float iat_corr = 293.0f / (g_iat_c + 273.0f);  // density ref: 20°C = 293K
  float clt_enr  = clt_enrichment(g_clt_c);
  float stft_cor = 1.0f + g_stft / 100.0f;
  float pw = (float)g_base_pw_us * (ve / 100.0f) * iat_corr * clt_enr * stft_cor;
  if (pw < 500.0f)    pw = 500.0f;
  if (pw > 20000.0f)  pw = 20000.0f;
  return (uint32_t)pw;
}

// ---------------------------------------------------------------
// Injector pulse-off — esp_timer callback (high-priority task context)
// ---------------------------------------------------------------
static void inj_off_cb(void*) {
  digitalWrite(ECU_INJ1_PIN, LOW);
#ifdef ECU_INJ2_PIN
  digitalWrite(ECU_INJ2_PIN, LOW);
#endif
}

static void fire_injectors(uint32_t pw_us) {
  if (pw_us == 0 || g_fuel_cut) return;
  esp_timer_stop(g_inj_off_timer);  // cancel any previous in-flight pulse
  digitalWrite(ECU_INJ1_PIN, HIGH);
#ifdef ECU_INJ2_PIN
  digitalWrite(ECU_INJ2_PIN, HIGH);
#endif
  esp_timer_start_once(g_inj_off_timer, pw_us);
}

// ---------------------------------------------------------------
// NVS persistence
// ---------------------------------------------------------------
static void load_nvs() {
  Preferences p;
  p.begin(NVS_NAMESPACE, true);
  g_mode        = p.getUChar("ecu_mode", 0xFF);
  float afr     = p.getFloat("ecu_afr",  -1.0f);
  uint32_t bpw  = p.getUInt("ecu_bpw",  0);
  p.end();

  if (g_mode > 1) g_mode = 0xFF;  // sentinel: let hw switch decide below

  if (afr > 5.0f && afr < 25.0f)
    g_target_afr = afr;
  // else: keep value set by ecu_setup() from ENABLE_ECU macros

  if (bpw > 200 && bpw < 30000)
    g_base_pw_us = bpw;
  // else: keep computed value from calc_base_pw()
}

static void save_nvs() {
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putUChar("ecu_mode", g_mode);
  p.putFloat("ecu_afr",  g_target_afr);
  p.putUInt("ecu_bpw",   g_base_pw_us);
  p.end();
}

// ---------------------------------------------------------------
// Carb PI controller — runs at ECU_CARB_PWM_FREQ_HZ cadence
// ---------------------------------------------------------------
static void carb_pi_tick() {
  if (g_afr < 5.0f) return;  // no valid WBO2 reading
  static uint32_t last_ms = 0;
  uint32_t now = millis();
  float dt = (now - last_ms) / 1000.0f;
  if (last_ms == 0 || dt > 1.0f) { last_ms = now; return; }
  last_ms = now;

  float err = g_target_afr - g_afr;  // +err → running rich → need more air → raise duty
  g_integral += err * dt;

  // Anti-windup: clamp integral so it can't wind beyond duty limits
  float max_int = (ECU_CARB_DUTY_MAX - 50.0f) / (ECU_CARB_KI + 0.001f);
  float min_int = (ECU_CARB_DUTY_MIN - 50.0f) / (ECU_CARB_KI + 0.001f);
  if (g_integral > max_int) g_integral = max_int;
  if (g_integral < min_int) g_integral = min_int;

  g_carb_duty = 50.0f + ECU_CARB_KP * err + ECU_CARB_KI * g_integral;
  if (g_carb_duty < ECU_CARB_DUTY_MIN) g_carb_duty = ECU_CARB_DUTY_MIN;
  if (g_carb_duty > ECU_CARB_DUTY_MAX) g_carb_duty = ECU_CARB_DUTY_MAX;

  uint32_t duty_raw = (uint32_t)(g_carb_duty * 1023.0f / 100.0f + 0.5f);
  CARB_PWM_WRITE(duty_raw);
}

// ---------------------------------------------------------------
// Inject STFT update — called every ~250 ms when engine running
// ---------------------------------------------------------------
static void stft_update() {
  if (g_afr < 5.0f || g_rpm < 100) return;
  static uint32_t last_ms = 0;
  uint32_t now = millis();
  if (now - last_ms < 250) return;
  last_ms = now;

  float err = g_target_afr - g_afr;
  g_stft += err * ECU_CARB_KI * 0.25f * 100.0f;  // integrate at carb KI rate
  if (g_stft >  ECU_STFT_MAX_PCT) g_stft =  ECU_STFT_MAX_PCT;
  if (g_stft < -ECU_STFT_MAX_PCT) g_stft = -ECU_STFT_MAX_PCT;
}

// ---------------------------------------------------------------
// ECU_DATA broadcast helper
// ---------------------------------------------------------------
static void broadcast_ecu_data(uint8_t flags) {
  uint8_t d[8] = {};
  d[0] = g_mode;
  d[1] = (uint8_t)((int)g_map_kpa < 0 ? 0 : (int)g_map_kpa > 255 ? 255 : (int)g_map_kpa);
  d[2] = (uint8_t)((int)g_tps_pct < 0 ? 0 : (int)g_tps_pct > 100 ? 100 : (int)g_tps_pct);
  // Temperatures encoded as °C + 40 (so −40°C = 0, 0°C = 40, 80°C = 120)
  int clt_enc = (int)(g_clt_c + 40.5f);
  int iat_enc = (int)(g_iat_c + 40.5f);
  d[3] = (uint8_t)(clt_enc < 0 ? 0 : clt_enc > 255 ? 255 : clt_enc);
  d[4] = (uint8_t)(iat_enc < 0 ? 0 : iat_enc > 255 ? 255 : iat_enc);
  // pw_val: duty×100 for carb mode, or pulse width μs for inject mode
  uint32_t pw_val = (g_mode == 0) ? (uint32_t)(g_carb_duty * 100.0f) : g_pw_us;
  if (pw_val > 65535) pw_val = 65535;
  d[5] = (uint8_t)(pw_val & 0xFF);
  d[6] = (uint8_t)(pw_val >> 8);
  d[7] = flags;
  bus_tx(CAN_ID_ECU_DATA, d, 8);
}

// ---------------------------------------------------------------
// Per-mode loops
// ---------------------------------------------------------------
static void carb_loop() {
  static uint32_t last_sensor_ms = 0;
  static uint32_t last_bcast_ms  = 0;
  uint32_t now = millis();

  if (now - last_sensor_ms >= 100) {
    last_sensor_ms = now;
    sensor_sample();
  }

  carb_pi_tick();

  if (now - last_bcast_ms >= 250) {
    last_bcast_ms = now;
    uint8_t flags = 0;
    if (g_afr > 5.0f)                          flags |= 0x01;  // closed_loop
    if (g_rpm > 100)                            flags |= 0x08;  // running
    broadcast_ecu_data(flags);
  }
}

static void inject_loop() {
  static uint32_t last_sensor_ms = 0;
  static uint32_t last_bcast_ms  = 0;
  uint32_t now_ms = millis();
  uint32_t now_us = micros();

  if (now_ms - last_sensor_ms >= 50) {
    last_sensor_ms = now_ms;
    sensor_sample();
    g_pw_us = calc_pulse_width();
    stft_update();
  }

  if (now_ms - last_bcast_ms >= 250) {
    last_bcast_ms = now_ms;
    uint8_t flags = 0;
    if (g_afr > 5.0f) flags |= 0x01;
    if (g_stft > 5.0f) flags |= 0x02;
    if (g_rpm > 100)  flags |= 0x08;
    if (g_rpm > 100 && g_pw_us > 0) {
      uint32_t window = 60000000UL / g_rpm;
      if (g_pw_us > window * 95 / 100) flags |= 0x04;  // saturated
    }
    broadcast_ecu_data(flags);
  }

  // Fire injectors once per crankshaft revolution
  if (!g_fuel_cut && g_rpm >= 100) {
    uint32_t interval_us = 60000000UL / g_rpm;
    if ((uint32_t)(now_us - g_last_inj_us) >= interval_us) {
      // Advance by one interval; if we've drifted more than 2 intervals, re-sync
      g_last_inj_us += interval_us;
      if ((uint32_t)(now_us - g_last_inj_us) > interval_us * 2)
        g_last_inj_us = now_us;
      fire_injectors(g_pw_us);
    }
  }
}

// ---------------------------------------------------------------
// Startup — compute BASE_PW_US from engine displacement + injector size
//
// base_pw_us = fuel_mass_per_cyl_g / injector_flow_g_per_us
//   fuel_mass = (disp_cc/cyl × air_density_g_cc) / stoich_afr
//   flow_g_per_us = n_inj × cc_min × fuel_density_g_cc / 60 / 1e6
//
// 351W dual 750cc/min: ~3250 μs at 100 kPa, 14.7 AFR, 20°C
// ---------------------------------------------------------------
static void calc_base_pw() {
  float disp_per_cyl_cc = (float)ECU_DISPLACEMENT_CC / (float)ECU_CYLINDERS;
  float air_mass_g      = disp_per_cyl_cc * 0.001225f;
  float fuel_mass_g     = air_mass_g / 14.7f;
  float n_inj = 1.0f;
#ifdef ECU_INJ2_PIN
  n_inj = 2.0f;
#endif
  float flow_g_per_us = n_inj * (float)ECU_INJ_CC_MIN * 0.737f / 60.0f / 1e6f;
  g_base_pw_us = (uint32_t)(fuel_mass_g / flow_g_per_us + 0.5f);
  Serial.printf("[ecu] base_pw=%u us  disp/cyl=%.0f cc  n_inj=%.0f\n",
                g_base_pw_us, disp_per_cyl_cc, n_inj);
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------
void ecu_setup() {
  // Compute base pulse width from config constants
  calc_base_pw();

  // Defaults from node config (NVS may override below)
  g_target_afr = ECU_CARB_TARGET_AFR;  // same default for both modes

  load_nvs();  // may override g_base_pw_us, g_target_afr, g_mode

  // Sensor input pins
  pinMode(ECU_MAP_PIN, INPUT);
  pinMode(ECU_TPS_PIN, INPUT);
  pinMode(ECU_CLT_PIN, INPUT);
  pinMode(ECU_IAT_PIN, INPUT);
  pinMode(ECU_MODE_SWITCH_PIN, INPUT_PULLUP);

  // If NVS had no stored mode, read hardware switch
  if (g_mode == 0xFF) {
    g_mode = (digitalRead(ECU_MODE_SWITCH_PIN) == HIGH) ? 1 : 0;
  }

  // Carb solenoid (LEDC) — always set up; neutral 50%
#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(ECU_CARB_SOLENOID_PIN, ECU_CARB_PWM_FREQ_HZ, 10);
  ledcWrite(ECU_CARB_SOLENOID_PIN, 512);
#else
  ledcSetup(ECU_CARB_LEDC_CH, ECU_CARB_PWM_FREQ_HZ, 10);
  ledcAttachPin(ECU_CARB_SOLENOID_PIN, ECU_CARB_LEDC_CH);
  CARB_PWM_WRITE(512);
#endif

  // Injector pins — always set up low; only fire in mode 1
  pinMode(ECU_INJ1_PIN, OUTPUT);
  digitalWrite(ECU_INJ1_PIN, LOW);
#ifdef ECU_INJ2_PIN
  pinMode(ECU_INJ2_PIN, OUTPUT);
  digitalWrite(ECU_INJ2_PIN, LOW);
#endif

  // esp_timer for pulse-off (used in inject mode)
  esp_timer_create_args_t ta = {};
  ta.callback = inj_off_cb;
  ta.name     = "inj_off";
  esp_timer_create(&ta, &g_inj_off_timer);

  Serial.printf("[ecu] mode=%u target_afr=%.1f base_pw=%u us\n",
                g_mode, g_target_afr, g_base_pw_us);
}

void ecu_loop() {
  if (g_mode == 0) carb_loop();
  else             inject_loop();
}

void ecu_handle_frame(const BusFrame& f) {
  // Cache RPM — published by this node's own mod_rpm via self-echo
  if (f.id == CAN_ID_ENGINE_DATA && f.dlc >= 2) {
    g_rpm = unpack_u16(f.data);
    return;
  }
  // Cache AFR — published by this node's own mod_wbo2 via self-echo
  if (f.id == CAN_ID_WBO2_DATA && f.dlc >= 2) {
    g_afr = unpack_u16(f.data) / 100.0f;
    return;
  }
  // ECU commands
  if (f.id != CAN_ID_ECU_CMD || f.dlc < 2) return;
  switch (f.data[0]) {
    case 0x01:  // set mode
      g_mode = f.data[1] & 0x01;
      if (g_mode == 1) {
        // Entering inject: neutralize carb solenoid
        CARB_PWM_WRITE(512);
        g_integral = 0.0f;
      } else {
        // Entering carb: close injectors
        esp_timer_stop(g_inj_off_timer);
        digitalWrite(ECU_INJ1_PIN, LOW);
#ifdef ECU_INJ2_PIN
        digitalWrite(ECU_INJ2_PIN, LOW);
#endif
        g_stft = 0.0f;
      }
      save_nvs();
      Serial.printf("[ecu] mode=%u\n", g_mode);
      break;

    case 0x02:  // set target AFR×100 (bytes 1+2, uint16 LE)
      if (f.dlc >= 3) {
        float afr = unpack_u16(f.data + 1) / 100.0f;
        if (afr > 5.0f && afr < 25.0f) {
          g_target_afr = afr;
          save_nvs();
          Serial.printf("[ecu] target_afr=%.2f\n", g_target_afr);
        }
      }
      break;

    case 0x03:  // fuel cut (arg0: 0=off 1=on)
      g_fuel_cut = (f.data[1] != 0);
      if (g_fuel_cut) {
        esp_timer_stop(g_inj_off_timer);
        digitalWrite(ECU_INJ1_PIN, LOW);
#ifdef ECU_INJ2_PIN
        digitalWrite(ECU_INJ2_PIN, LOW);
#endif
      }
      Serial.printf("[ecu] fuel_cut=%u\n", g_fuel_cut);
      break;

    case 0x04:  // reset fuel trim
      g_stft = 0.0f;
      g_integral = 0.0f;
      Serial.println("[ecu] trim reset");
      break;
  }
}

#endif // ENABLE_ECU
