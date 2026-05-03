// Node configuration for the ECU ESP32.
// Copied to accessory_node/node_config.h by the Makefile before compiling.
//
// This node reads:
//   WBO2 analog (0–5V controller output)  RPM pulse (distributor/coil via PC817C optocoupler)
//   MAP sensor (MPX4250AP or similar 0–5V)  TPS (potentiometer 0–5V)
//   CLT + IAT (NTC thermistors with 10k pull-up to 3.3V)
// And drives:
//   Mode 1 — closed-loop carb: mixture control solenoid via MOSFET (12 Hz PWM)
//   Mode 2 — TBI injection: dual fuel injectors via MOSFETs (microsecond pulse, esp_timer)
//
// Wiring quick-ref (all analog inputs go through a 100kΩ+100kΩ 2:1 voltage divider
// to bring 0–5V sensor output into the 0–3.3V ESP32 ADC range):
//   GPIO 34 — RPM pulse (PC817C collector; external 10k pull-up to 3V3, no internal pull-up)
//   GPIO 35 — WBO2 analog (÷2 divider; input-only)
//   GPIO 36 — MAP sensor  (÷2 divider; input-only)
//   GPIO 39 — TPS         (÷2 divider; input-only)
//   GPIO 32 — CLT NTC     (10k pull-up to 3.3V; read raw, convert via Beta model)
//   GPIO 33 — IAT NTC     (10k pull-up to 3.3V)
//   GPIO 13 — Mode switch (external 10k pull-up to 3V3; LOW=carb, HIGH=inject)
//   GPIO 25 — Carb mixture solenoid MOSFET gate (10k pull-down + 100Ω series)
//   GPIO 26 — Injector 1 MOSFET gate            (10k pull-down + 100Ω series)
//   GPIO 27 — Injector 2 MOSFET gate            (10k pull-down + 100Ω series)
//
// MOSFET: IRLZ44N (logic-level, 55V/47A) for all three outputs.
// Flyback diode: 1N5822 Schottky (cathode to +12V, anode to MOSFET drain) on each output.
//
// Injector sizing for 351 Windsor V8 (5752cc):
//   Dual 750cc/min (≈105 lb/hr each) recommended — single injector saturates above ~2500 RPM WOT.
//   For single-injector use: comment out ECU_INJ2_PIN and accept limited WOT fuelling.

#pragma once

#define NODE_NAME           "ecu"
#define NODE_ID             0x04
#define USE_CAN_TRANSCEIVER 1
#define CAN_BUS_SPEED       125   // kbps — change all nodes together: 125, 250, or 500
#define USE_WIFI            1
#include "secrets.h"
#define AP_HIDDEN           0
#define NVS_NAMESPACE       "ecu"

// ---- RPM sensor ----
// PC817C collector → GPIO 34. Connect 10kΩ from GPIO 34 to 3V3 (no internal pull-up on 34).
// Coil (–) → 270Ω → PC817C pin 1 (anode). PC817C pin 4 → 3V3. Pin 3 (collector) → GPIO 34.
#define ENABLE_RPM
#define RPM_PIN         34
#define RPM_CYLINDERS   8       // V8; adjust for your engine
#define RPM_SAMPLE_MS   250     // faster sample for ECU closed-loop use

// ---- Wideband O2 sensor ----
// Analog output of standalone WBO2 controller (Innovate LC-2, AEM X-Series, etc.)
// Run the 0–5V output through a 100kΩ+100kΩ voltage divider → GPIO 35.
// Calibrate WBO2_MIN_V / WBO2_MAX_V to match your controller's output spec.
// Innovate LC-2: 0V=7.35 AFR, 5V=22.39 AFR (lambda 0.5–1.52); typical street range 10–20 AFR.
#define ENABLE_WBO2
#define WBO2_PIN          35      // input-only; connect through 2:1 voltage divider
#define WBO2_SAMPLE_MS    100     // 10 Hz for closed-loop control
#define WBO2_MIN_V        0.0f    // after divider: sensor 0V → ADC 0V  (adjust if controller differs)
#define WBO2_MAX_V        2.5f    // after divider: sensor 5V → ADC 2.5V
#define WBO2_MIN_AFR      10.0f
#define WBO2_MAX_AFR      20.0f

// ---- ECU dual-mode fuel control ----
#define ENABLE_ECU

// Physical mode switch — LOW=carb closed-loop, HIGH=TBI injection.
// Wire: 10kΩ from GPIO 13 to 3V3, toggle switch between GPIO 13 and GND.
#define ECU_MODE_SWITCH_PIN       13

// ---- Mode 1: closed-loop carburetor ----
// A mixture-control solenoid (air-bleed type) is driven at 12 Hz PWM.
// Higher duty cycle = more air bleed = leaner mixture.
// Neutral duty (50%) = no correction. PI controller adjusts based on WBO2 error.
// KP / KI tune notes: start conservative (KP=1.5, KI=0.3); increase if response is sluggish.
#define ECU_CARB_SOLENOID_PIN     25      // MOSFET gate; 10k pull-down + 100Ω series
#define ECU_CARB_LEDC_CH          0       // LEDC channel 0; must not conflict with ENABLE_BUZZER
#define ECU_CARB_PWM_FREQ_HZ      12
#define ECU_CARB_TARGET_AFR       14.7f   // stoichiometric default; tune to taste
#define ECU_CARB_KP               1.5f    // proportional gain (duty% per AFR error unit)
#define ECU_CARB_KI               0.3f    // integral gain (duty% per AFR error per second)
#define ECU_CARB_DUTY_MIN         10      // minimum solenoid duty % (don't fully close)
#define ECU_CARB_DUTY_MAX         90      // maximum solenoid duty % (don't fully open)

// ---- Mode 2: throttle-body injection ----
// Injector(s) fire on a calculated interval derived from current RPM.
// Each injection delivers fuel for one cylinder event (1/8 of the 4-stroke cycle for a V8).
// Two injectors fire simultaneously to double effective flow — required for a 351W at WOT.
//
// Wire each MOSFET identically:
//   ESP32 GPIO → 100Ω → MOSFET Gate; 10kΩ from Gate to GND; Drain to injector (−); Source to GND.
//   1N5822 Schottky: cathode to +12V, anode to Drain (MOSFET drain side of injector coil).
//   Injector (+): switched +12V (via relay or fuse; do NOT use the relay-controller relays — size
//   the fuse for your injectors, typically 5–10A per injector).
#define ECU_INJ1_PIN              26      // MOSFET gate for injector 1 (required)
#define ECU_INJ2_PIN              27      // MOSFET gate for injector 2 (comment out for single-injector)

// Injector flow rate (both injectors must be the same model).
// With ECU_INJ2_PIN defined, both fire simultaneously → effective flow = 2 × this value.
// 351 Windsor sizing: 750 cc/min (≈ 105 lb/hr) each × 2 = 1500 cc/min total.
#define ECU_INJ_CC_MIN            750     // cc/min per injector at rated fuel pressure (3 bar / 43.5 psi)

// Displacement and cylinder count (used to compute BASE_PW_US at startup).
#define ECU_DISPLACEMENT_CC       5752    // 351 CID = 5752cc
#define ECU_CYLINDERS             8       // must match RPM_CYLINDERS

// Closed-loop AFR target for injection mode.
#define ECU_INJ_TARGET_AFR        14.7f   // stoichiometric; override via CAN at runtime

// Maximum short-term fuel trim — WBO2 closed-loop correction is clamped to ±this%.
// If the trim rail hits this limit it means BASE_PW_US needs adjustment.
#define ECU_STFT_MAX_PCT          30

// MAP sensor — MPX4250AP (0.2V–4.9V for 0–250 kPa; connect through 2:1 voltage divider).
// Atmosphere ≈ 100 kPa; idle vacuum ≈ 25–40 kPa; WOT ≈ 95–100 kPa.
// Formula (after divider, where Vadc = raw sensor voltage / 2):
//   kPa = (Vadc × 2.0 − 0.2) × 250.0 / (4.9 − 0.2)
#define ECU_MAP_PIN               36      // input-only; connect through 2:1 voltage divider

// TPS — 0–5V potentiometer; connect through 2:1 voltage divider → GPIO 39.
// Closed-throttle: ~0.5V raw (0.25V after divider). WOT: ~4.5V raw (2.25V after divider).
// Calibrate ECU_TPS_CLOSED_MV / ECU_TPS_OPEN_MV after install (millivolts at ADC pin).
#define ECU_TPS_PIN               39      // input-only; connect through 2:1 voltage divider
#define ECU_TPS_CLOSED_MV         125     // mV at ADC pin when throttle fully closed
#define ECU_TPS_OPEN_MV           2250    // mV at ADC pin at wide-open throttle

// CLT / IAT — GM-style 10kΩ NTC thermistors.
// Wire: 3.3V → 10kΩ pull-up resistor → GPIO pin → NTC → GND.
// Beta model: Beta=3540, R0=2590Ω at 25°C (matches standard GM sensor).
#define ECU_CLT_PIN               32      // coolant temp
#define ECU_IAT_PIN               33      // intake air temp
#define ECU_NTC_PULLUP_OHMS       10000   // pull-up resistor value
#define ECU_NTC_BETA              3540    // thermistor Beta coefficient
#define ECU_NTC_R0                2590    // Ω at T0 (25°C)
