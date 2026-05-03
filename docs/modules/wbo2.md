# Module: mod_wbo2

Wideband O2 (lambda) sensor analog read. Reads the 0–5V analog output from any LSU 4.9 wideband O2 controller, converts to AFR via a configurable linear map, and broadcasts on the CAN bus. The ECU module consumes these frames for closed-loop fuel correction.

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
| `WBO2_OVERSAMPLE` | `16` | Number of ADC samples averaged per reading. Higher values reduce ESP32 ADC noise at the cost of a slightly longer read time. Default 16. |
| `WBO2_ADC_VREF` | `3.3f` | ADC full-scale voltage in V. The module sets 11 dB attenuation automatically; actual full-scale is ~3.9 V but 3.3 V is conservative and accurate enough for most setups. |
| `WBO2_EMA_ALPHA` | *(not set)* | Exponential moving average factor (0.0–1.0). **Omit to disable** — no EMA is applied by default. Lower values smooth more aggressively: `0.2` gives ~400 ms time constant at 10 Hz; `0.5` is light smoothing. Only needed if noise remains after hardware improvements. |
| `WBO2_MIN_V` | `0.0f` | Voltage **at the ADC pin** (after divider) corresponding to `WBO2_MIN_AFR`. |
| `WBO2_MAX_V` | `2.5f` | Voltage **at the ADC pin** (after divider) corresponding to `WBO2_MAX_AFR`. A 2:1 divider from 5V → 2.5V max. |
| `WBO2_MIN_AFR` | `10.0f` | AFR at `WBO2_MIN_V` |
| `WBO2_MAX_AFR` | `20.0f` | AFR at `WBO2_MAX_V` |

The AFR-to-voltage mapping is linear between `MIN` and `MAX`. Calibration varies by controller — refer to your controller's datasheet or calibrate empirically at stoich (14.7 AFR) against a known reference.

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

## Signal integrity

The controller's analog output is low-frequency (DC to a few Hz useful bandwidth), so true coax is unnecessary. These steps in rough order of impact:

**1. Use 10 kΩ + 10 kΩ divider resistors, not 100 kΩ.**
Lower resistance reduces source impedance at the ADC pin by 10×, making it far less susceptible to pickup. Current draw is still only ~250 µA — not a concern.

**2. Add an RC filter at the ADC pin.**
A 1 kΩ series resistor between the divider output and the ESP32 pin, plus a 100 nF cap from that pin to GND, forms a ~1.6 kHz low-pass filter. Place the cap as close to the ESP32 pin as possible. This kills ignition, alternator, and injector noise.

```
Controller 0–5V ──[10kΩ]──┬──[10kΩ]── GND
                           └──[1kΩ]── WBO2_PIN ──[100nF]── GND
```

**3. Use shielded cable for the wire run.**
Any automotive shielded wire (or shielded twisted pair) works. Connect the shield to ground **at the controller end only** — grounding both ends creates a ground loop that can make noise worse.

**4. Ensure a solid shared ground.**
The #1 cause of bad analog readings in automotive setups is a voltage difference between the controller board's GND and the ESP32's GND. Run a dedicated ground wire between the two, or verify both connect to the same chassis ground point (not opposite ends of the car).

**5. Software EMA filter (`WBO2_EMA_ALPHA`).**
If noise is still visible after the hardware steps, define `WBO2_EMA_ALPHA` in the node config to apply an exponential moving average across readings. Start at `0.3f` and tune from there. This is a last resort — hardware filtering is more effective and doesn't add lag to the closed-loop response.

---

## Integration notes

- `wbo2_loop()` must be called every `loop()`.
- `WBO2_MIN_V` / `WBO2_MAX_V` are the **post-divider** voltages at the ADC pin, not the raw 0–5V controller output. A common mistake is entering 0.0 and 5.0 — this causes a 2× AFR calibration error.
- The module calls `analogSetAttenuation` (11 dB) in setup automatically, expanding the ADC input range to ~3.9 V. No manual attenuation setup needed.
- Most LSU 4.9 controllers take ~30–60 seconds to warm up. The module broadcasts readings immediately, but values will be invalid until warm-up is complete.
- ADC2 (GPIOs 0, 2, 4, 12–15, 25–27) is unavailable while WiFi is active. `WBO2_PIN` must be on ADC1 (GPIOs 32–39).
- Cross-check calibration: at stoich idle (14.7 AFR), the ADC pin should read approximately `(14.7 - WBO2_MIN_AFR) / (WBO2_MAX_AFR - WBO2_MIN_AFR) × WBO2_MAX_V` volts.
