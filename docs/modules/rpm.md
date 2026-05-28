# Module: mod_rpm

Engine RPM measurement via PC817C optocoupler interrupt counting, with an optional LCD bar widget. Taps the ignition coil's negative terminal (same point as a traditional tachometer).

## Enable

```cpp
#define ENABLE_RPM
```

---

## Config defines

| Define | Example | Description |
|--------|---------|-------------|
| `RPM_PIN` | `34` / `35` | GPIO for PC817C collector output. **Input-only pin (34/35 on ECU; 35 on relay controller) — no internal pull-up.** Requires external 10 kΩ to 3.3V. |
| `RPM_CYLINDERS` | `8` | Coil fires per crankshaft revolution. V8 distributor = 8 (4 fires per cam revolution, 2 cam turns per crank). 6-cylinder = 6, 4-cylinder = 4. |
| `RPM_SAMPLE_MS` | `500` | How often to compute and broadcast RPM. Shorter = more responsive closed-loop use; longer = smoother display. |
| `RPM_REDLINE` | `6500` | *(optional)* Initial redline for LCD bar widget. Also configurable at runtime via CAN. |
| `RPM_WIDGET_ROW` | `1` | *(optional)* LCD row for the RPM bar. Omit to disable the bar widget. |

---

## CAN frames

### Sends

| ID | Name | Payload | Rate |
|----|------|---------|------|
| `0x304` | `ENGINE_DATA` | `[rpm_lo, rpm_hi]` | Every `RPM_SAMPLE_MS`. uint16 LE. |

```
# Example: 850 RPM idle (0x0352)
304  52 03

# Example: 3500 RPM (0x0DAC)
304  AC 0D
```

### Receives

| ID | Name | Handled |
|----|------|---------|
| `0x400` | `CONFIG_WRITE` | Key `0x40` (RPM redline) updates the LCD bar widget scale. |

---

## RPM bar widget (LCD)

When `RPM_WIDGET_ROW` is defined and `ENABLE_LCD` is active, an 8-character bar graph is registered at the specified row. The bar fills proportionally from 0 RPM to the configured redline.

Set redline at runtime via CAN (persists to NVS with `flags = 0x01`):
```
400 01 40 00 00 64 19 01    set redline = 6500 RPM on switch_panel
400 02 40 00 00 64 19 01    set redline = 6500 RPM on relay_controller
```

---

## Hardware wiring

```
Ignition coil (–) ──[270Ω]──── PC817C pin 1 (LED +)
                                PC817C pin 2 (LED –) ──── GND

3.3V ──[10kΩ]──┬── PC817C pin 3 (collector) ──── RPM_PIN
                └── (pull-up)
                    PC817C pin 4 (emitter) ──── GND
```

The 270 Ω resistor limits LED current to ~14 mA at 12 V during dwell. Each coil fire pulls RPM_PIN LOW; the firmware counts falling edges via interrupt.

---

## Integration notes

- RPM_PIN has no internal pull-up on any of the usual GPIO choices (34, 35). The external 10 kΩ is mandatory — without it the pin floats between coil fires and counts noise.
- `RPM_CYLINDERS` must match the actual coil fire pattern. For a V8 distributor: one revolution of the distributor (cam shaft) fires 4 cylinders; two distributor revolutions per one crank revolution = 4 fires × 2 = 8 per crank revolution.
- On HEI distributors, tap the tach output terminal on the distributor cap rather than the coil negative terminal — the coil negative on HEI may not produce a clean square wave.
- Two nodes can run `ENABLE_RPM` simultaneously (e.g. relay_controller and ecu_node both monitor RPM). Both will broadcast `ENGINE_DATA (0x304)` — other nodes will see two frames per sample period. The ECU uses its own reading for closed-loop; other display nodes consume either.
- [`mod_fuel_pump`](fuel_pump.md) on the relay controller consumes `ENGINE_DATA` as one of its safety gates. RPM source doesn't matter — the FSM accepts the frame regardless of which node broadcast it.
