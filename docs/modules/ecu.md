# Module: mod_ecu

Dual-mode fuel controller. Mode 0 runs a PI closed-loop on a carb air-bleed mixture-control solenoid. Mode 1 drives dual throttle-body fuel injectors with RPM/MAP/VE-table pulse-width calculation and short-term fuel trim from WBO2. Reads MAP, TPS, CLT, IAT sensors and subscribes to `WBO2_DATA` frames from [mod_wbo2](wbo2.md).

## Enable

```cpp
#define ENABLE_ECU
// also typically:
#define ENABLE_WBO2
#define ENABLE_RPM
```

---

## Config defines

### Sensors

| Define | Example | Description |
|--------|---------|-------------|
| `ECU_MAP_PIN` | `36` | MAP sensor ADC (input-only, 2:1 divider from 0.2–4.9V) |
| `ECU_TPS_PIN` | `39` | TPS potentiometer ADC (input-only, 2:1 divider from 0–5V) |
| `ECU_CLT_PIN` | `32` | Coolant temp NTC thermistor (10 kΩ pull-up to 3.3V) |
| `ECU_IAT_PIN` | `33` | Intake air temp NTC thermistor (10 kΩ pull-up to 3.3V) |
| `ECU_NTC_BETA` | `3540` | NTC Beta coefficient |
| `ECU_NTC_R0` | `2590` | NTC resistance at 25 °C (Ω) |
| `ECU_NTC_PULLUP_OHMS` | `10000` | Pull-up resistor value (Ω) |

**Sensor pins must be on ADC1 (GPIOs 32–39).** ADC2 is unavailable when WiFi is active.

### Mode selection

| Define | Example | Description |
|--------|---------|-------------|
| `ECU_MODE_SWITCH_PIN` | `13` | GPIO for physical mode switch. HIGH = injection, LOW = carb. Uses `INPUT_PULLUP` (GPIO 13 is a strapping pin — use normally-open switch). |

### Carb mode (Mode 0)

| Define | Example | Description |
|--------|---------|-------------|
| `ECU_CARB_SOLENOID_PIN` | `25` | MOSFET gate for carb solenoid. 100 Ω series + 10 kΩ pull-down to GND. |
| `ECU_CARB_LEDC_CH` | `0` | LEDC channel (0–15). Must not conflict with other LEDC users. Do not use `ENABLE_BUZZER` on the same node (it uses Arduino `tone()`, which uses a different timer and does not conflict). |
| `ECU_CARB_PWM_FREQ_HZ` | `12` | Solenoid drive frequency. 12 Hz is typical for Rochester Q-jet / feedback Holley. |
| `ECU_CARB_TARGET_AFR` | `14.7f` | Initial target AFR. Override at runtime via CAN. |
| `ECU_CARB_KP` | `1.5f` | PI proportional gain (duty% per AFR error unit) |
| `ECU_CARB_KI` | `0.3f` | PI integral gain (duty% per AFR error per second) |
| `ECU_CARB_DUTY_MIN` | `10` | Minimum duty % — prevents fully closing the air bleed (no-start risk) |
| `ECU_CARB_DUTY_MAX` | `90` | Maximum duty % |

### Injection mode (Mode 1)

| Define | Example | Description |
|--------|---------|-------------|
| `ECU_INJ1_PIN` | `26` | MOSFET gate for injector 1. Required. |
| `ECU_INJ2_PIN` | `27` | *(optional)* MOSFET gate for injector 2. Comment out for single-injector. |
| `ECU_INJ_CC_MIN` | `750` | Injector flow rate in cc/min at rated fuel pressure (3 bar / 43.5 psi) |
| `ECU_DISPLACEMENT_CC` | `5752` | Engine displacement in cc (351 CID = 5752 cc) |
| `ECU_CYLINDERS` | `8` | Cylinder count. Must match `RPM_CYLINDERS`. |
| `ECU_INJ_TARGET_AFR` | `14.7f` | Initial injection target AFR. |
| `ECU_STFT_MAX_PCT` | `30` | Short-term fuel trim clamp (±30%). Trim railing = `BASE_PW` needs adjustment. |

---

## CAN frames

### Sends

| ID | Name | Payload | Rate |
|----|------|---------|------|
| `0x307` | `ECU_DATA` | `[mode, map_kpa, tps_pct, clt_enc, iat_enc, pw_lo, pw_hi, flags]` | ~250 ms |

`ECU_DATA` payload:
| Byte | Field | Notes |
|------|-------|-------|
| 0 | mode | 0 = carb, 1 = injection |
| 1 | map_kpa | MAP reading in kPa |
| 2 | tps_pct | Throttle position 0–100% |
| 3 | clt_enc | Coolant temp: °C + 40 (80 °C → 120) |
| 4 | iat_enc | Intake air temp: °C + 40 |
| 5–6 | pw (uint16 LE) | Carb: duty × 100; Injection: pulse width µs |
| 7 | flags | bit0=closed_loop, bit1=enriching, bit2=inj_saturated, bit3=running |

### Receives

| ID | Name | Payload | Behaviour |
|----|------|---------|-----------|
| `0x308` | `ECU_CMD` | `[cmd, arg0, arg1, arg2]` | Runtime control commands |
| `0x306` | `WBO2_DATA` | `[afr_lo, afr_hi]` | Used for closed-loop correction in both modes |
| `0x400` | `CONFIG_WRITE` | target=0x04 | Keys 0x51 (mode), 0x52 (target AFR), 0x53 (base pulse width) |

#### ECU_CMD commands

| `data[0]` | Command | Arguments |
|-----------|---------|-----------|
| `0x01` | Set mode | `arg0`: 0 = carb, 1 = injection |
| `0x02` | Set target AFR | `arg1 + arg2` uint16 LE, AFR × 100 |
| `0x03` | Fuel cut | `arg0`: 0 = off, 1 = on |
| `0x04` | Reset STFT | Clears short-term fuel trim to 0% |

```
308 01 01 00 00    switch to injection mode
308 01 00 00 00    switch to carb mode
308 02 00 BE 05    set target AFR = 14.70 (0x05BE)
308 03 01 00 00    fuel cut ON
308 04 00 00 00    reset STFT
```

Config keys via CONFIG_WRITE:
```
400 04 51 00 00 01 00 00 01    ECU mode = injection, persist
400 04 52 00 00 00 BE 05 01    target AFR = 14.70, persist
400 04 53 00 00 00 A0 0F 01    base PW = 4000 µs, persist
```

---

## BASE_PW tuning

At startup the serial log prints the computed base pulse width:
```
[ecu] base_pw=3250 us  disp/cyl=719 cc  n_inj=2
```

This is the pulse width at 100% VE and 100 kPa MAP. If the engine runs consistently rich or lean across the RPM range, `BASE_PW` needs adjustment. Short-term fuel trim (STFT) railing at ±30% is the signal:
- Trim at **+30%** = engine lean → increase `BASE_PW`
- Trim at **−30%** = engine rich → decrease `BASE_PW`

---

## Hardware wiring summary

```
ECU_MAP_PIN (36) ←── 100kΩ ÷ 100kΩ divider from MPX4250AP 0.2–4.9V
ECU_TPS_PIN (39) ←── 100kΩ ÷ 100kΩ divider from throttle pot 0–5V
WBO2_PIN    (35) ←── 100kΩ ÷ 100kΩ divider from wideband controller 0–5V
ECU_CLT_PIN (32) ←── 10kΩ pull-up to 3.3V + NTC to GND
ECU_IAT_PIN (33) ←── 10kΩ pull-up to 3.3V + NTC to GND
RPM_PIN     (34) ←── 10kΩ pull-up to 3.3V + PC817C collector

ECU_CARB_SOLENOID_PIN (25) ──[100Ω]── IRLZ44N gate; [10kΩ] gate-to-GND
ECU_INJ1_PIN          (26) ──[100Ω]── IRLZ44N gate; [10kΩ] gate-to-GND
ECU_INJ2_PIN          (27) ──[100Ω]── IRLZ44N gate; [10kΩ] gate-to-GND
```

All injector and solenoid +12V runs must be **separately fused** and wired directly from the fuse block — do not share with the relay controller supply rail.

---

## Integration notes

- `ecu_handle_frame()` must be called for every frame returned by `bus_rx()`.
- Injector pulse timing uses `esp_timer_start_once()` for the close event — microsecond accuracy independent of `loop()` timing. At 3000+ RPM, pulse widths of 2–6 ms require this precision.
- Use IRLZ44N (logic-level N-MOSFET) for all outputs. Standard IRFZ44N / IRF540 may only partially conduct at 3.3V gate voltage and will not switch cleanly or may dissipate excessive heat.
- Flyback diodes (1N5822, cathode to +12V, anode to MOSFET drain) are mandatory on all inductive loads. A missing or backwards diode will destroy the MOSFET within seconds.
