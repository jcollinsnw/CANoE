# Module: mod_gps

GPS speed and heading via NMEA UART. Parses `$GPRMC` sentences from a u-blox Neo-6M / Neo-8M (or compatible) module and broadcasts speed and heading on the CAN bus each time a valid fix arrives.

## Enable

```cpp
#define ENABLE_GPS
```

---

## Config defines

| Define | Example | Description |
|--------|---------|-------------|
| `GPS_SERIAL_NUM` | `2` | Which hardware UART to use (0, 1, or 2). Serial2 is typical. |
| `GPS_RX_PIN` | `16` | ESP32 UART RX ← GPS TX. Must match `GPS_SERIAL_NUM`'s RX mux. |
| `GPS_TX_PIN` | `17` | ESP32 UART TX → GPS RX. Only needed if sending config commands to the GPS module. Leave unconnected for read-only. |
| `GPS_BAUD` | `9600` | Baud rate. u-blox Neo-6M/8M default is 9600. |

---

## CAN frames

### Sends

| ID | Name | Payload | Rate |
|----|------|---------|------|
| `0x305` | `GPS_DATA` | `[speed_lo, speed_hi, heading_lo, heading_hi, flags]` | ~1 Hz, on each valid `$GPRMC` sentence. Only sent when fix is valid. |

Payload encoding:
- **speed** — uint16 LE, speed in 0.1 mph. Divide by 10 for mph. e.g. `0x01C2` = 450 → 45.0 mph.
- **heading** — uint16 LE, heading in 0.1 degrees true north. e.g. `0x0384` = 900 → 90.0°.
- **flags** — `bit 0` = fix valid, `bit 1` = speed valid, `bit 2` = heading valid.

```
# Example: 45.0 mph (450=0x01C2), heading 270.0° (2700=0x0A8C), fix valid (0x07)
305  C2 01 8C 0A 07
```

### Receives

None.

---

## Hardware wiring

```
GPS VCC ──── 3.3V (check module — some require 5V via VIN)
GPS GND ──── GND
GPS TX  ──── GPS_RX_PIN
GPS RX  ──── GPS_TX_PIN  (optional — leave unconnected for read-only)
```

u-blox modules output NMEA at 9600 baud by default. The module starts broadcasting once it has power; no init sequence is required for basic reading.

---

## Integration notes

- `gps_loop()` must be called every `loop()`.
- No frame is broadcast until a valid fix is obtained. During acquisition, the `$GPRMC` sentence is received but flags remain unset.
- GPS antenna placement matters for cold-start time. Placing the antenna near a metal surface reduces signal. An external active antenna dramatically improves indoor/tunnel acquisition.
- Speed and heading are only valid when `flags & 0x02` and `flags & 0x04` respectively, independent of the fix flag.
