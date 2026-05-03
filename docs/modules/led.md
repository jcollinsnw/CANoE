# Module: mod_led

CAN-addressable status LED driver. Any node on the bus can set LEDs on any node with `ENABLE_LEDS` by sending a `LED_CMD` frame with the target node's ID.

## Enable

```cpp
#define ENABLE_LEDS
```

---

## Config defines

| Define | Example | Description |
|--------|---------|-------------|
| `NUM_LEDS` | `3` | Number of LED outputs |
| `LED_PINS_INIT` | `{17, 19, 23}` | GPIO for each LED (index 0-based) |
| `LED_ACTIVE_HIGH` | `true` | `true` = GPIO HIGH turns LED on (anode to GPIO via resistor, cathode to GND). `false` = GPIO LOW turns LED on (common-anode wiring). |

---

## CAN frames

### Receives

| ID | Name | Payload | Behaviour |
|----|------|---------|-----------|
| `0x102` | `LED_CMD` | `[target_node_id, mask, state]` | If `target_node_id` matches this node's ID or is `0xFF` (broadcast), applies the mask/state to the LED outputs. Only LEDs whose bit is set in `mask` are changed. |

### Sends

| ID | Name | Payload | Triggered by |
|----|------|---------|--------------|
| `0x103` | `LED_STATUS` | `[node_id, bitmap]` | Sent whenever LED state changes. Bit 0 = LED 1, etc. |

---

## LED bitmap

```
bit 0 = LED 1 (0x01)
bit 1 = LED 2 (0x02)
bit 2 = LED 3 (0x04)
```

Examples:
```
102 01 07 07    switch_panel (0x01): all 3 LEDs on
102 01 07 00    switch_panel: all 3 LEDs off
102 01 01 01    LED 1 on only (LEDs 2 and 3 unchanged)
102 01 06 02    LED 2 on, LED 3 off (mask touches bits 1–2)
102 FF 07 07    broadcast: all LEDs on all ENABLE_LEDS nodes
```

---

## Mirroring relay state with rules

A common use is to mirror relay state onto LEDs using rules on the switch panel node. Because `RELAY_STATUS (0x101)` is broadcast at 5 Hz, the rules fire continuously:

```cpp
RULE(TRIG_RELAY_BIT_ON(0),  ACT_LED_ON(0x01, 0)),   // relay 1 ON  → LED 1 on
RULE(TRIG_RELAY_BIT_OFF(0), ACT_LED_OFF(0x01, 0)),  // relay 1 OFF → LED 1 off
```

---

## Integration notes

- `led_handle_frame()` must be called for every frame returned by `bus_rx()`.
- `LED_ACTIVE_HIGH true` is standard for LEDs wired with the anode to the GPIO (GPIO → resistor → LED → GND). Flip if your circuit uses a common-anode LED bar or active-low driver.
- Series resistors: 330 Ω is a conservative starting point at 3.3V. Adjust for LED forward voltage and desired brightness.
