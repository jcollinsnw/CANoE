# Module: mod_dht22

AM2302 (DHT22) temperature and humidity sensor. Reads the sensor on a configurable interval and broadcasts the result on the CAN bus.

## Enable

```cpp
#define ENABLE_DHT22
```

---

## Config defines

| Define | Example | Description |
|--------|---------|-------------|
| `DHT22_PIN` | `15` | GPIO connected to the sensor's data line. Any digital GPIO works. Pull up to 3.3V via a 4.7–10 kΩ resistor (many AM2302 modules include one). |
| `DHT_INTERVAL_MS` | `5000` | How often to sample and broadcast. Minimum 2000 ms (AM2302 hardware limit). |

---

## CAN frames

### Sends

| ID | Name | Payload | Rate |
|----|------|---------|------|
| `0x301` | `ENV_DATA` | `[temp_lo, temp_hi, humi_lo, humi_hi]` | Every `DHT_INTERVAL_MS` |

Payload encoding:
- **temp** — int16 LE, temperature in 0.1 °C units. e.g. `0x00F0` = 240 → 24.0 °C. Negative values are two's complement.
- **humi** — uint16 LE, relative humidity in 0.1 % units. e.g. `0x01F4` = 500 → 50.0 %RH.

```
# Example: 24.5°C (245 = 0x00F5), 58.3%RH (583 = 0x0247)
301  F5 00 47 02
```

### Receives

None.

---

## Integration notes

- `dht22_loop()` must be called every `loop()`.
- The AM2302 requires at least 2 seconds between readings. Setting `DHT_INTERVAL_MS` below 2000 will result in read errors — the firmware ignores failed reads silently.
- If the sensor returns `NaN` (failed read), no frame is broadcast for that cycle.
