# Module: mod_fuel_pump

Multi-gate fuel pump safety FSM with on-board ignition coil voltage sampling. Owns the fuel pump relay and cycles it like a modern fuel-injected car: prime on boot, off while waiting for evidence the engine is alive, on while the engine is running, off again when that evidence goes away.

Why it exists: the antique car's master switch is a **chassis-ground kill switch** that disconnects the battery when off but keeps the relay box running continuously when on. With no key-controlled relay between the accessory battery and the relay node, "ESP32 boot" does not imply "driver is in the car." The OEM pump burned out the first time after a `TRIG_BOOT → ACT_RELAY_ON(R1)` rule turned the pump on, the user parked at a restaurant with the master switch on and the key out, and the pump dead-headed against a closed float valve for hours. This module replaces that boot rule with an FSM that requires actual engine-alive evidence before commanding the pump on.

> **History note:** the coil voltage sensing used to live in a separate `mod_ignition` module and reach the FSM via the self-echoed `IGNITION_DATA` frame. They were merged because the only producer was `mod_ignition` and the only consumer was `mod_fuel_pump` on the same node — the round trip added complexity without any real decoupling. The `IGNITION_DATA (0x310)` frame is still broadcast for observability (the Cardputer FEED screen decodes it).

## Enable

```cpp
#define ENABLE_FUEL_PUMP_SAFETY
```

Currently configured on the relay controller node only.

---

## Config defines

### Fuel pump FSM

| Define | Example | Description |
|--------|---------|-------------|
| `FUEL_PUMP_RELAY` | `0` | Relay index (0-5) for the fuel pump. R1 = index 0. |
| `FUEL_PUMP_PRIME_MS` | `3000` | Initial prime duration after boot or re-enable. Pump is ON. |
| `FUEL_PUMP_RPM_THRESHOLD` | `200` | RPM above which the RPM gate is considered passing. Below cranking RPM (~150–200) so it captures crank as a "yes." |
| `FUEL_PUMP_STALL_MS` | `2000` | A gate is "stale" if it hasn't been freshly passing within this window. If any enabled gate is stale, pump goes off. |
| `FUEL_PUMP_DEFAULT_MODE` | `3` | Initial mode on boot. See modes below. |

### Coil voltage sense (optional — omit `IGN_COIL_ADC_PIN` to disable the COIL gate)

| Define | Example | Description |
|--------|---------|-------------|
| `IGN_COIL_ADC_PIN` | `39` | ADC1 input-only pin connected to the coil-sense voltage divider. ADC1 (GPIO 32-39) is mandatory — ADC2 conflicts with WiFi. |
| `IGN_COIL_DIVIDER_RATIO` | `5.545f` | (R_top + R_bot) / R_bot. With 10 kΩ + 2.2 kΩ → ratio 5.545, max input ~18 V (13 V actual / 5.545 = 2.34 V at ADC, safely below 3.3 V). |
| `IGN_COIL_ON_THRESHOLD_CV` | `600` | Centivolts. Voltage must rise above this for `coil_on` to flip true. 600 cv = 6.00 V — well below ~9 V (key-RUN) and well above 0 V (key-out). |
| `IGN_COIL_OFF_THRESHOLD_CV` | `400` | Hysteresis lower bound. Voltage must drop below this for `coil_on` to flip back to false. The gap between ON and OFF thresholds defeats jitter around the threshold during cranking. |
| `IGN_COIL_SAMPLE_MS` | `200` | ADC poll interval. |
| `IGN_COIL_BROADCAST_MS` | `1000` | Periodic heartbeat broadcast interval. Edge transitions broadcast immediately regardless. |

---

## Gates

The FSM has two independent gates. Each tracks its own "last seen fresh" timestamp.

| Gate | Source | Pass condition |
|------|--------|----------------|
| RPM | `CAN_ID_ENGINE_DATA (0x304)` (from any node) | `rpm >= FUEL_PUMP_RPM_THRESHOLD` |
| COIL | internal hysteresis-debounced coil ADC state | `coil_on == true` |

A gate is "currently passing" if its last-fresh timestamp is within `FUEL_PUMP_STALL_MS` of now. The pump is permitted to run iff **every enabled gate** is currently passing (AND semantics).

The RPM gate consumes whatever node publishes `ENGINE_DATA` — usually `mod_rpm` on the relay node, but the ECU node also publishes it. The FSM doesn't care which node sent it.

The COIL gate is driven by the local coil ADC, sampled internally — there is no CAN round trip. (`IGNITION_DATA` is still broadcast for observability but the FSM doesn't consume it.)

---

## Mode bitmask

`mode` is a uint8 bitmask where bit 0 = RPM gate enabled, bit 1 = COIL gate enabled. Adding new gates (e.g. oil pressure as bit 2) extends naturally — up to 8 gates fit in a single byte.

| Value | Name | RPM gate | COIL gate | Behavior |
|-------|------|----------|-----------|----------|
| `0` | `FP_MODE_OFF` | disabled | disabled | Both gates off — pump forced ON, FSM frozen. |
| `1` | `FP_MODE_RPM` | enabled | disabled | RPM-only. Catches "no spark" failure modes. |
| `2` | `FP_MODE_COIL` | disabled | enabled | COIL-only. Catches "key out" failure modes. |
| `3` | `FP_MODE_BOTH` | enabled | enabled | Strictest. AND of both — survives a single sensor failure since the other gate still gates the pump. **Default on the relay node.** |

---

## FSM

```
boot
  ↓
PRIME (pump ON, FUEL_PUMP_PRIME_MS)
  ↓ timer elapsed
ARMED (pump OFF, waiting)
  ↓ all enabled gates passing
RUNNING (pump ON, monitoring)
  ↓ any enabled gate stale for FUEL_PUMP_STALL_MS
ARMED (pump OFF, reason=STALL)
```

The FSM is **advisory, not authoritative** — it only emits a `RELAY_CMD` on state transitions. Manual relay toggles (web UI, Cardputer, rules) work between transitions and persist until the next transition. This makes bench debugging painless: turn the pump on by hand to test, the FSM doesn't fight you until something changes.

---

## CAN frames

### Receives

| ID | Name | Behavior |
|----|------|----------|
| `0x304` | `ENGINE_DATA` | If `rpm >= threshold`, updates `g_last_rpm_ok_ms`. |
| `0x400` | `CONFIG_WRITE` | Key `0x61 CFG_KEY_FUEL_PUMP_SAFETY`: data[4] is the new mode bitmask. |

> Note: `IGNITION_DATA (0x310)` is **broadcast** by this module but not consumed — the coil state is sampled internally from the ADC.

### Sends

| ID | Name | Payload | When |
|----|------|---------|------|
| `0x100` | `RELAY_CMD` | `[mask, state]` for `FUEL_PUMP_RELAY` | Every state transition. |
| `0x310` | `IGNITION_DATA` | `[coil_cv_lo, coil_cv_hi, ign_on]` | Heartbeat every `IGN_COIL_BROADCAST_MS`, plus immediately on every coil on/off edge. |
| `0x311` | `FUEL_PUMP_STATE` | `[state, mode, reason, gates_ok]` | Every state transition. |
| `0x104` | `BUZZER_CMD` | `[0xFF, BUZZER_SEQ_FUEL_PUMP_OFF, 0]` | Stall transitions only (RUNNING → ARMED with reason=STALL). |

`FUEL_PUMP_STATE` payload:
- `state`: 0=PRIME, 1=ARMED, 2=RUNNING
- `mode`: current FuelPumpMode bitmask
- `reason`: 0=none, 1=boot, 2=prime_done, 3=gate_pass, 4=**stall**, 5=mode_change, 6=re_enable
- `gates_ok`: bit 0 = RPM gate currently passing, bit 1 = COIL gate currently passing

`IGNITION_DATA` payload:
- `coil_cv` — int16 LE, signed centivolts (e.g. 920 = 9.20 V).
- `ign_on` — 1 if voltage is above the on-threshold (after hysteresis), 0 otherwise.

---

## Hardware — coil sense

```
Coil + ──[10 kΩ]──┬──[2.2 kΩ]── GND
                   └── IGN_COIL_ADC_PIN
```

Tap the coil + side (after the dash ballast resistor), not the coil – side. Coil – is the high-voltage switching side used for tachometer signals — wrong signal entirely for "is the coil powered." Coil – goes to `RPM_PIN` via the PC817C optocoupler, documented in [mod_rpm.md](rpm.md).

The wiring schematic at [assets/coil-interface-schematic.svg](../../assets/coil-interface-schematic.svg) puts both the coil-sense divider and the RPM optocoupler on a single signal-conditioning board that sits between the relay box and the coil wiring.

### Hysteresis behavior

A clean rise above 6.00 V → `coil_on = 1`. A clean fall below 4.00 V → `coil_on = 0`. Anything in the 4.00–6.00 V band holds the previous state. This prevents chatter during cranking when the coil sees a sawtooth of brief 12 V spikes between dwell intervals — the average voltage stays well above 6 V so `coil_on` stays true.

---

## Runtime control

### Via CAN

```
# Set mode to BOTH (gates: RPM + COIL)
400 02 61 00 00 03 00 00 00

# Set mode to COIL only
400 02 61 00 00 02 00 00 00

# Set mode to RPM only
400 02 61 00 00 01 00 00 00

# Disable safety (pump forced ON, FSM frozen)
400 02 61 00 00 00 00 00 00
```

State is **RAM-only**. A reboot restores `FUEL_PUMP_DEFAULT_MODE`. If you want a runtime change to survive a reboot, change the `#define` and reflash — this is intentional, the safety should default to on.

### Via rules

```cpp
RULE(TRIG_SW_LONG(7), ACT_FUEL_PUMP_SAFETY_DISABLE),     // long-press SW8 → pump always on
RULE(TRIG_SW_LONG(6), ACT_FUEL_PUMP_SAFETY_BOTH),        // long-press SW7 → strictest mode
RULE(TRIG_SW_LONG(5), ACT_FUEL_PUMP_SAFETY_COIL_ONLY),   // long-press SW6 → COIL-only
```

Available macros (all call `fuel_pump_set_mode()`):
- `ACT_FUEL_PUMP_SAFETY(mode)` — parametric form, mode = 0–3
- `ACT_FUEL_PUMP_SAFETY_DISABLE` — mode 0
- `ACT_FUEL_PUMP_SAFETY_RPM_ONLY` — mode 1
- `ACT_FUEL_PUMP_SAFETY_COIL_ONLY` — mode 2
- `ACT_FUEL_PUMP_SAFETY_BOTH` — mode 3
- `ACT_FUEL_PUMP_SAFETY_ENABLE` — alias for RPM_ONLY (back-compat with the old single-gate API)

---

## Audible alerts

On a stall transition (RUNNING → ARMED with reason=STALL), the FSM broadcasts:
1. `BUZZER_CMD` with `BUZZER_SEQ_FUEL_PUMP_OFF` — the switch panel's `mod_buzzer` plays a distinctive 4-pulse low/high alarm.
2. `FUEL_PUMP_STATE` with `reason=4` — the Cardputer's `m5_handle_frame` routes the matching `BUZZER_CMD` to `m5_beep_alert()` and shows "FUEL PUMP CUT" in the CLI bar.

Non-stall transitions (boot, prime done, gate pass, mode change, re-enable) emit `FUEL_PUMP_STATE` but not the buzzer command — those are expected, not faults.

---

## Re-enable behavior

Transitions from `FP_MODE_OFF` back to any other mode restart the FSM from PRIME. If the engine is already running, the pump stays on through PRIME (3 s), then briefly OFF during ARMED (until the next ENGINE_DATA frame arrives or the next coil sample confirms — usually < 500 ms), then back ON in RUNNING. The carb bowl easily bridges that ~500 ms gap.

---

## Integration notes

- Calling `fuel_pump_setup()` emits an immediate `RELAY_CMD` to turn the pump on for PRIME. Make sure `relay_setup()` ran first so the pin is initialized.
- The FSM consumes RPM from `ENGINE_DATA` regardless of source. If both the relay node and the ECU node have `ENABLE_RPM`, both will broadcast `ENGINE_DATA` and the FSM just sees more frames.
- Switching mode mid-RUNNING resets the FSM to PRIME, not ARMED — the rationale is that the user might be re-enabling after a long pause and wants the bowl refilled.
- `IGN_COIL_ADC_PIN` is optional. Omit it and the COIL gate is permanently failing; only `FP_MODE_OFF` and `FP_MODE_RPM` work. The default mode in that case should be set to 1 (RPM) in the node config.
- ESP32 ADC has known non-linearity, especially at the extremes. The 6 V on-threshold is well into the linear region of a 5.545:1 divider (~1.1 V at the ADC pin), so calibration isn't critical for the boolean detection. If you want accurate voltage telemetry rather than just on/off, calibrate against a multimeter at a known coil voltage.
- Future gate (oil pressure as bit 2): the gate needs a grace period right after PRIME because oil pressure takes 1–2 seconds to build after the engine starts. Either extend PRIME or add a per-gate "post-prime startup window" before the stall timer starts running.
