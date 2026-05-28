# Module: mod_ignition

Reads the ignition coil's + side voltage through an ADC and broadcasts it on the bus. Provides the "key in / coil powered" signal that `mod_fuel_pump` uses as its COIL gate.

Why a separate module: the chassis-ground master switch on this car keeps the relay node powered continuously, regardless of whether the key is in. There's no key-controlled feed already reaching the ESP32. Tapping the coil + line (after the dash ballast resistor) gives a clean ~9 V signal when the key is in RUN, ~12 V during cranking, and 0 V key-out — enough to drive the fuel pump FSM and anything else that wants "is the engine being driven?" as a signal.

## Enable

```cpp
#define ENABLE_IGNITION
```

Currently configured on the relay controller node only.

---

## Config defines

| Define | Example | Description |
|--------|---------|-------------|
| `IGN_COIL_ADC_PIN` | `39` | ADC1 input-only pin connected to the coil-sense voltage divider. ADC1 (GPIO 32-39) is mandatory — ADC2 conflicts with WiFi. |
| `IGN_COIL_DIVIDER_RATIO` | `5.545f` | (R_top + R_bot) / R_bot. With 10 kΩ + 2.2 kΩ → ratio 5.545, max input ~18 V (13 V actual / 5.545 = 2.34 V at ADC, safely below 3.3 V). |
| `IGN_COIL_ON_THRESHOLD_CV` | `600` | Centivolts. Voltage must rise above this for `ign_on` to flip true. 600 cv = 6.00 V — well below ~9 V (key-RUN) and well above 0 V (key-out). |
| `IGN_COIL_OFF_THRESHOLD_CV` | `400` | Hysteresis lower bound. Voltage must drop below this for `ign_on` to flip back to false. The gap between ON and OFF thresholds defeats jitter around the threshold during cranking. |
| `IGN_COIL_SAMPLE_MS` | `200` | ADC poll interval. |
| `IGN_COIL_BROADCAST_MS` | `1000` | Periodic heartbeat broadcast interval. Edge transitions broadcast immediately regardless. |

---

## CAN frames

### Sends

| ID | Name | Payload | When |
|----|------|---------|------|
| `0x310` | `IGNITION_DATA` | `[coil_cv_lo, coil_cv_hi, ign_on]` | Heartbeat every `IGN_COIL_BROADCAST_MS`, plus immediately on every on/off edge. |

Payload:
- `coil_cv` — int16 LE, signed centivolts (e.g. 920 = 9.20 V).
- `ign_on` — 1 if voltage is above the on-threshold (after hysteresis), 0 otherwise.

```
# Example: coil at 9.20 V, on
310  98 03 01

# Example: coil at 0.00 V, off (key out)
310  00 00 00
```

---

## Hardware wiring

```
Coil + ──[10 kΩ]──┬──[2.2 kΩ]── GND
                   └── IGN_COIL_ADC_PIN
```

Tap the coil + side (after the dash ballast resistor), not the coil – side. Coil – is the high-voltage switching side used for tachometer signals — wrong signal entirely for "is the coil powered."

| Pin | Signal | Connection |
|-----|--------|------------|
| 10 kΩ top leg | from coil + | upstream of divider |
| 2.2 kΩ bottom leg | to GND | downstream of divider |
| `IGN_COIL_ADC_PIN` | divider midpoint | ESP32 ADC1 input-only |

GPIO 39 (default) has no internal pull-up, but you don't want one here — the divider is the only thing setting the voltage.

---

## Hysteresis behavior

```
   Coil voltage
      │
12 V ─┤             ┌──────┐
      │             │      │
      │ ON_THRESH ──┼──────┼──────  6.00 V
      │             │      │
      │ OFF_THRESH ─┼──────┼──────  4.00 V
      │             │      │
  0 V ┤─────────────┘      └─────
      └─────────────────────────── time
```

A clean rise above 6.00 V → `ign_on = 1`. A clean fall below 4.00 V → `ign_on = 0`. Anything in the 4.00–6.00 V band holds the previous state. This prevents chatter during cranking when the coil sees a sawtooth of brief 12 V spikes between dwell intervals — the average voltage stays well above 6 V so `ign_on` stays true.

---

## Integration notes

- `ignition_setup()` does a single ADC read to seed the initial state before the first periodic poll. This avoids a brief "off" reading on boot when the engine is already running.
- The module broadcasts immediately on every edge so consumers (the fuel pump FSM, the Cardputer FEED screen) react with minimal latency. Heartbeats every 1 s keep the COIL gate in `mod_fuel_pump` fresh even if no edges have occurred.
- ESP32 ADC has known non-linearity, especially at the extremes. The 6 V on-threshold is well into the linear region of a 5.545:1 divider (~1.1 V at the ADC pin), so calibration isn't critical for the boolean detection. If you want accurate voltage telemetry rather than just on/off, calibrate against a multimeter at a known coil voltage.
- The voltage reported in `coil_cv` is computed from the same `IGN_COIL_DIVIDER_RATIO` — re-tune if your resistors aren't exactly 10 kΩ and 2.2 kΩ.
