# Module: mod_wbo2

Wideband O2 (lambda) sensor analog read. Reads the 0–5V analog output from a wideband O2 controller (Innovate LC-2, AEM X-Series, or equivalent), converts to AFR, and broadcasts on the CAN bus. The ECU module consumes these frames for closed-loop fuel correction.

## Enable

```cpp
#define ENABLE_WBO2
```

---

## Config defines

| Define | Example | Description |
|--------|---------|-------------|
| `WBO2_PIN` | `35` | ADC1 input GPIO. **Input-only — no internal pull-up.** Must be on ADC1 (GPIOs 32–39); ADC2 is unavailable when WiFi is active. Connect via 2:1 voltage divider from the controller's 0–5V output. |
| `WBO2_SAMPLE_MS` | `100` | Sample and broadcast interval in ms. 100 ms (10 Hz) is recommended for closed-loop ECU use. |
| `WBO2_MIN_V` | `0.0f` | Voltage **at the ADC pin** (after divider) corresponding to `WBO2_MIN_AFR`. |
| `WBO2_MAX_V` | `2.5f` | Voltage **at the ADC pin** (after divider) corresponding to `WBO2_MAX_AFR`. A 2:1 divider from 5V → 2.5V max. |
| `WBO2_MIN_AFR` | `10.0f` | AFR at `WBO2_MIN_V` |
| `WBO2_MAX_AFR` | `20.0f` | AFR at `WBO2_MAX_V` |

The AFR-to-voltage mapping is linear between `MIN` and `MAX`. Refer to your controller's datasheet for the exact calibration. Innovate LC-2 default: 0V = 7.35 AFR, 5V = 22.39 AFR.

---

## CAN frames

### Sends

| ID | Name | Payload | Rate |
|----|------|---------|------|
| `0x306` | `WBO2_DATA` | `[afr_lo, afr_hi]` | Every `WBO2_SAMPLE_MS`. uint16 LE, AFR × 100. |

```
# Example: 14.70 AFR (1470 = 0x05BE)
306  BE 05

# Example: 12.50 AFR (1250 = 0x04E2)
306  E2 04
```

### Receives

None directly. The ECU module subscribes to `WBO2_DATA` frames for closed-loop correction — see [mod_ecu](ecu.md).

---

## Hardware wiring

```
Wideband controller 0–5V output ──[100kΩ]──┬──[100kΩ]── GND
                                             └── WBO2_PIN (ADC sees 0–2.5V)
```

The 2:1 voltage divider with 100 kΩ resistors keeps load current low (~25 µA) and avoids affecting the controller's output stage. With 100 kΩ + 100 kΩ the ADC pin sees half the controller voltage.

---

## Integration notes

- `wbo2_loop()` must be called every `loop()`.
- `WBO2_MIN_V` / `WBO2_MAX_V` are the **post-divider** voltages at the ADC pin, not the raw 0–5V controller output. A common mistake is entering 0.0 and 5.0 — this causes a 2× AFR calibration error.
- The wideband controller takes time to warm up (Innovate LC-2: ~60 seconds). During warm-up it outputs a fixed voltage — the module will broadcast readings, but they won't be valid until warm-up is complete.
- ADC2 (GPIOs 0, 2, 4, 12–15, 25–27) is unavailable while WiFi is active. `WBO2_PIN` must be on ADC1 (GPIOs 32–39).
- Cross-check calibration: at stoich idle (14.7 AFR), the ADC pin should read approximately `(14.7 - WBO2_MIN_AFR) / (WBO2_MAX_AFR - WBO2_MIN_AFR) × WBO2_MAX_V` volts.
