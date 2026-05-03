# CAN Bus Reference

This document covers how to use the accessory bus at runtime — sending commands, querying state, configuring behavior, and managing rules. All examples are shown as raw hex frames in the format used by the web console command line.

```
<id_hex> <byte0> <byte1> ...    # hex, space-separated, up to 8 bytes
```

---

## Node IDs

| Node | ID |
|------|----|
| switch_panel | 0x01 |
| relay_controller | 0x02 |
| viper_interface | 0x03 |
| ecu_node | 0x04 |
| bridge | 0x05 |

---

## Node Management

### Node heartbeat — `0x0F0 NODE_ANNOUNCE`

Every node broadcasts this every 5 seconds. `data[0]` carries the sender's `node_id` so all nodes share the same CAN ID.

| Byte | Field | Notes |
|------|-------|-------|
| 0 | node_id | sender's ID (0x01–0x05) |
| 1 | peer_count | number of active ESP-NOW peers seen |
| 2 | can_ok | 1 = CAN bus healthy, 0 = down |

### Boot event — `0x0F1 BOOT_EVENT`

Emitted once at end of `setup()`, self-echoed back into the RX ring. Used by `TRIG_BOOT()` in the rules engine to fire actions on power-up.

| Byte | Field |
|------|-------|
| 0 | node_id |

```
# Read boot event on the bus (just observe — nodes emit this automatically)
# Use TRIG_BOOT() in RULES_DEFAULT_INIT to react to it
```

### Node capability advertisement — `0x0F2 NODE_CAP`

Every node broadcasts this at boot and every 30 seconds. Also sent in response to a `NODE_CAP_REQ`. The bridge uses these frames to build its aggregated control panel.

| Byte | Field | Notes |
|------|-------|-------|
| 0 | node_id | sender's ID |
| 1 | caps | bitmask: 0x01=relay, 0x02=switches, 0x04=viper, 0x08=leds, 0x10=rules |
| 2 | switch_count | number of latching switches |
| 3 | button_count | number of momentary buttons |
| 4 | led_count | number of CAN-controllable LEDs |
| 5 | relay_count | number of relay outputs |

### Request capabilities — `0x0F3 NODE_CAP_REQ`

Ask one or all nodes to immediately send their `NODE_CAP` frame.

```
# Request caps from all nodes
0F3 FF

# Request caps from relay controller only
0F3 02
```

---

## Relay Control

### Toggle / set individual relays — `0x100 RELAY_CMD`

Payload: `[mask, state]`

- **mask** — which relays to act on. Bit 0 = relay 1, bit 5 = relay 6.
- **state** — desired state for each masked relay. 1 = on, 0 = off.

Only relays whose bit is set in mask are changed. All others keep their current state.

```
# Relay 1 on
100 01 01

# Relay 1 off
100 01 00

# Relay 2 on
100 02 02

# Relay 3 on
100 04 04

# Relay 4 on
100 08 08

# Relay 5 on  (horn — subject to 30 s safety cutoff)
100 10 10

# Relay 6 on
100 20 20

# All 6 relays off
100 3F 00

# All 6 relays on
100 3F 3F

# Relay 1 on, relay 2 off, relays 3-6 unchanged
100 03 01
```

**Relay bitmap reference:**

| Relay | Bit | Hex |
|-------|-----|-----|
| 1 | bit 0 | 0x01 |
| 2 | bit 1 | 0x02 |
| 3 | bit 2 | 0x04 |
| 4 | bit 3 | 0x08 |
| 5 | bit 4 | 0x10 |
| 6 | bit 5 | 0x20 |
| All | bits 0–5 | 0x3F |

### Read relay state — `0x101 RELAY_STATUS`

The relay controller broadcasts current relay state every 200 ms automatically. You don't need to request it — just watch the frame log. Byte 0 is the relay bitmap using the same bit assignments above.

```
# Example received frame — relays 1 and 3 on:
101  05
```

---

## LED Control

### Set LEDs — `0x102 LED_CMD`

Payload: `[target_node_id, mask, state]`

- **target** — node ID to address (0xFF = broadcast to all nodes with ENABLE_LEDS)
- **mask** — which LEDs to act on (bit 0 = LED 1, etc.)
- **state** — desired state for masked LEDs

```
# switch_panel (node 0x01): LED 1 on
102 01 01 01

# switch_panel: LED 1 off
102 01 01 00

# switch_panel: all 3 LEDs on
102 01 07 07

# switch_panel: all 3 LEDs off
102 01 07 00

# switch_panel: LED 2 on only (leaves LED 1 and 3 unchanged)
102 01 02 02

# Broadcast: all LEDs on all nodes on
102 FF 07 07
```

### Read LED state — `0x103 LED_STATUS`

Sent automatically by the target node whenever its LED state changes.

```
# Example: switch_panel (0x01) has LEDs 1 and 2 on
103  01 03
```

---

## Viper Alarm

### Send a command — `0x510 VIPER_CMD`

Payload: `[cmd]`

```
# Lock / arm
510 01

# Unlock / disarm
510 02

# Remote start
510 03
```

### Alarm response — `0x511 VIPER_STATUS`

The viper_interface node broadcasts the raw 5-byte alarm response packet whenever the alarm replies. Appears in the frame log automatically — no query needed.

---

## Switch Events (read-only)

The switch_panel broadcasts these automatically on every input change. Useful for monitoring or for triggering rules on other nodes.

### `0x200 SWITCH_EVENT`

Payload: `[switch_id, event]`

| Event | Value |
|-------|-------|
| Release | 0x00 |
| Press | 0x01 |
| Long press (≥ 600 ms) | 0x02 |
| Double press | 0x03 |

Switch IDs: 0–5 = SW1–SW6 (latching), 6–9 = BTN1–BTN4.

```
# Example: SW1 pressed
200  00 01

# Example: BTN2 long-pressed
200  07 02
```

### `0x201 ENCODER_EVENT`

Payload: `[event, count]`

| Event | Value |
|-------|-------|
| Rotate CW | 0x00 |
| Rotate CCW | 0x01 |
| Press | 0x02 |
| Release | 0x03 |
| Long press | 0x04 |

```
# Example: 2 clockwise detents
201  00 02
```

---

## LCD Text

### Write to the LCD — `0x500 LCD_CMD`

Payload: `[row, col, char, char, ...]` or `[0xFF]` to clear.

Addressed to the switch_panel (or any node with ENABLE_LCD).

```
# Clear the display
500 FF

# Write "Hello" at row 0, column 0
500 00 00 48 65 6C 6C 6F

# Write "World" at row 1, column 0
500 01 00 57 6F 72 6C 64

# Write at row 0, column 5
500 00 05 41 42 43
```

Row 0 is the status row (normally managed by the firmware). Row 1 is the event row. Writing to row 0 will be overwritten the next time `lcd_update_status()` fires.

---

## Telemetry (read-only)

### Battery voltage — `0x300 TELEMETRY`

Broadcast by the relay controller at 1 Hz when `VBAT_ADC_PIN` is defined.

Payload: `[vbat_cv_lo, vbat_cv_hi, ibatt_da_lo, ibatt_da_hi, vsolar_cv_lo, vsolar_cv_hi, flags, _]`

- **vbat_cv** — battery voltage in centvolts (little-endian int16). Divide by 100 for volts.
- Remaining fields reserved / unused in current firmware.

```
# Example: vbat = 0x04D2 = 1234 centvolts = 12.34 V
300  D2 04 00 00 00 00 00 00
```

### Engine RPM — `0x304 ENGINE_DATA`

Broadcast by the RPM node every `RPM_SAMPLE_MS` (default 500 ms).

Payload: `[rpm_lo, rpm_hi]` — uint16 little-endian.

```
# Example: 850 RPM idle (0x0352)
304  52 03

# Example: 3000 RPM (0x0BB8)
304  B8 0B
```

### GPS speed and heading — `0x305 GPS_DATA`

Broadcast by the GPS node each time a valid $GPRMC sentence arrives (~1 Hz).

Payload: `[speed_lo, speed_hi, heading_lo, heading_hi, flags]`

- **speed** — uint16 LE in 0.1 mph units. Divide by 10 for mph.
- **heading** — uint16 LE in 0.1 degree units. Divide by 10 for degrees true north.
- **flags** — bit 0: fix valid · bit 1: speed valid · bit 2: heading valid

```
# Example: 45.0 mph (450 = 0x01C2), heading 270.0° (2700 = 0x0A8C), fix valid (0x07)
305  C2 01 8C 0A 07
```

---

## Wideband O2 Sensor (WBO2)

### Air-Fuel Ratio (AFR) — `0x306 WBO2_DATA`

Broadcast by any node with a wideband O2 sensor (e.g., LSU 4.9 + controller) every `WBO2_SAMPLE_MS` (default 500 ms).

Payload: `[afr_lo, afr_hi]` — uint16 little-endian, AFR × 100.

- **afr_lo, afr_hi** — Air-Fuel Ratio (AFR), multiplied by 100. For example, 1470 = 14.70 AFR.

```
# Example: 14.70 AFR (0x05BE)
306  BE 05

# Example: 12.50 AFR (0x04E2)
306  E2 04
```

- The voltage-to-AFR mapping is linear and configurable in firmware (see `WBO2_MIN_V`, `WBO2_MAX_V`, `WBO2_MIN_AFR`, `WBO2_MAX_AFR`).
- Any node can listen for `0x306` frames to display or log AFR.
- If an LCD widget is configured, the AFR value will be shown on the display.

---

## ECU Node

### ECU sensor data — `0x307 ECU_DATA`

Broadcast by the ECU node when the engine is running. One frame per `ECU_SAMPLE_MS` (default 250 ms).

Payload: `[mode, map_kpa, tps_pct, clt_enc, iat_enc, pw_lo, pw_hi, flags]`

| Byte | Field | Notes |
|------|-------|-------|
| 0 | mode | 0 = carb, 1 = TBI injection |
| 1 | map_kpa | MAP sensor reading in kPa |
| 2 | tps_pct | Throttle position 0–100% |
| 3 | clt_enc | Coolant temp: °C + 40 (e.g. 80°C → 120) |
| 4 | iat_enc | Intake air temp: °C + 40 |
| 5–6 | pw (uint16 LE) | Carb: duty × 100 (e.g. 5000 = 50.00%); Injection: pulse width µs |
| 7 | flags | bit0=closed_loop, bit1=enriching, bit2=inj_saturated, bit3=running |

```
# Example: injection mode, 90 kPa MAP, 15% TPS, 80°C CLT, 25°C IAT, 3200 µs PW, running + closed loop
307  01 5A 0F 78 41 80 0C 09
```

### ECU commands — `0x308 ECU_CMD`

Send from any node or the web console to control the ECU at runtime.

Payload: `[cmd, arg0, arg1, arg2]`

| `data[0]` | Command | Arguments |
|-----------|---------|-----------|
| `0x01` | Set mode | `arg0`: 0 = carb, 1 = TBI injection |
| `0x02` | Set target AFR | `arg1 + arg2` = uint16 LE, AFR × 100 |
| `0x03` | Fuel cut | `arg0`: 0 = off, 1 = on |
| `0x04` | Reset fuel trim | — (clears STFT to 0%) |

```
# Switch to injection mode
308 01 01 00 00

# Switch to carb mode
308 01 00 00 00

# Set target AFR to 14.70 (0x05BE)
308 02 00 BE 05

# Set target AFR to 13.00 (0x0514)
308 02 00 14 05

# Fuel cut ON (e.g. decel)
308 03 01 00 00

# Fuel cut OFF
308 03 00 00 00

# Reset short-term fuel trim to 0%
308 04 00 00 00
```

Mode and target AFR are persisted to NVS. `BASE_PW` is changed via `CONFIG_WRITE` (target=0x04, key=0x53).

---

## Other Sensor Frames (read-only)

These frames are broadcast automatically by their respective modules — no query needed.

| ID | Frame | Payload summary |
|----|-------|----------------|
| `0x301` | `ENV_DATA` | `[temp_d1_lo, temp_d1_hi, humi_d1_lo, humi_d1_hi]` — 0.1 °C / 0.1 % from DHT22 |
| `0x302` | `IMU_DATA` | `[accel_x_lo, accel_x_hi, accel_y_lo, accel_y_hi, accel_z_lo, accel_z_hi]` — raw accelerometer |
| `0x303` | `SHAKE_EVENT` | `[magnitude, axis_mask]` — emitted by viper_interface on shake detection |

---

## Runtime Configuration

Config frames let you change node behavior over the bus without reflashing.

### Frame layout — `0x400 CONFIG_WRITE`

8 bytes: `[target, key, index, reserved, arg, arg2_lo, arg2_hi, flags]`

- **target** — node to configure (0x01 switch_panel, 0x02 relay_controller, 0xFF broadcast)
- **key** — what to configure (see keys below)
- **index** — which item (relay index, etc.)
- **arg** — primary value
- **arg2_lo / arg2_hi** — 16-bit secondary value (little-endian)
- **flags** — bit 0 = persist to NVS immediately

### Query current config — `0x401 CONFIG_READ_REQ`

3 bytes: `[target, key, index]` — use index `0xFF` to request all indices for a key.

The target node replies with one `0x402 CONFIG_READ_RESP` per index (same layout as CONFIG_WRITE).

### Save / reset config — `0x403 CONFIG_SAVE`

2 bytes: `[target, action]`

| Action | Value |
|--------|-------|
| Commit RAM → NVS | 0x01 |
| Reload NVS → RAM | 0x02 |
| Factory reset | 0x03 |

---

### Node ID reassignment — key `0x01`

Reassigns a node's ID, saves to NVS, and restarts the node immediately. Broadcast target (`0xFF`) is not accepted for this key.

`arg` (data[4]) = new node ID (0x01–0xFE).

```
# Reassign switch_panel (0x01) to ID 0x06, persist
400 01 01 00 00 06 00 00 01
```

---

### RPM redline — key `0x40`

Sets the RPM value that corresponds to a full bar on the LCD widget. Stored in NVS when the persist flag is set.

arg2 is the redline in RPM as a little-endian uint16 in bytes 5–6.

```
# Set redline to 6500 RPM on switch_panel (display node 0x01), persist immediately
# 6500 = 0x1964 → lo=0x64 hi=0x19
400 01 40 00 00 64 19 01

# Set redline to 5500 RPM (0x157C → lo=0x7C hi=0x15)
400 01 40 00 00 7C 15 01

# Broadcast to all nodes
400 FF 40 00 00 64 19 01
```

The target node echoes back a `0x402 CONFIG_READ_RESP` confirming the new value.

### Per-relay safety timeout — key `0x20`

Sets the maximum time a relay can stay on before the watchdog cuts it off. `0` means no limit.

```
# Set relay 5 (horn) to 10 second max (10000 ms = 0x2710)
# target=0x02 (relay_ctrl), key=0x20, index=4 (relay 5 is index 4), arg2=0x2710
400 02 20 04 00 00 10 27 00

# Same but persist to NVS immediately (flags=0x01)
400 02 20 04 00 00 10 27 01

# Remove limit on relay 5 (arg2 = 0)
400 02 20 04 00 00 00 00 01

# Query all relay timeouts
401 02 20 FF

# Save relay config to NVS
403 02 01

# Factory reset relay config
403 02 03
```

---

### WiFi enable / disable — key `0x30`

Legacy key — sets both SoftAP and ESP-NOW together. The node saves to NVS and restarts immediately.

```
# Disable WiFi on switch_panel (node 0x01)
400 01 30 00 00 00 00 00 00

# Enable WiFi on switch_panel
400 01 30 00 00 01 00 00 00

# Disable WiFi on all nodes (broadcast)
400 FF 30 00 00 00 00 00 00
```

### Independent SoftAP control — key `0x31`

Enable or disable only the SoftAP + web server, leaving ESP-NOW unaffected. Node restarts to apply.

```
# Disable AP on relay_controller (saves power if no one needs the web UI there)
400 02 31 00 00 00 00 00 00

# Re-enable AP on relay_controller
400 02 31 00 00 01 00 00 00
```

### Independent ESP-NOW control — key `0x32`

Enable or disable only the ESP-NOW radio. Node restarts to apply.

```
# Disable ESP-NOW on viper_interface (wired bus only)
400 03 32 00 00 00 00 00 00

# Re-enable
400 03 32 00 00 01 00 00 00
```

---

### ECU configuration — keys `0x51`, `0x52`, `0x53`

These can also be set via ECU_CMD (0x308) at runtime; CONFIG_WRITE persists them to NVS.

```
# Set ECU mode: carb (0) — target=0x04, key=0x51, arg=0
400 04 51 00 00 00 00 00 01

# Set ECU mode: injection (1)
400 04 51 00 00 01 00 00 01

# Set target AFR to 14.70 (0x05BE) — key=0x52, arg2=0x05BE
400 04 52 00 00 00 BE 05 01

# Set base pulse width to 3500 µs (0x0DAC) — key=0x53
400 04 53 00 00 00 AC 0D 01
```

---

## Rules Engine

Rules live in NVS on the switch_panel node and are evaluated against every CAN frame received (including self-echoed frames from that node itself). When a frame's ID matches `trig_id` and both byte conditions pass, the action fires immediately.

Rules are best managed through the web console **Rules tab**, but the underlying REST API is documented here for scripting.

### REST API (web console only, not CAN)

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/rules` | Return all rules as JSON |
| POST | `/api/rules` | Create or update a rule |
| DELETE | `/api/rules?idx=N` | Delete rule at index N |
| POST | `/api/rules/reset` | Factory reset — restores `RULES_DEFAULT_INIT` |

### How conditions work

Each rule has two independent byte conditions (c0 and c1). A condition is skipped when its mask is `0x00`. Both active conditions must match for the rule to fire.

```
Match: (frame.data[c_byte] & c_mask) == (c_val & c_mask)
```

### Trigger reference

| Macro | Trigger ID | Condition 0 | Condition 1 |
|-------|-----------|-------------|-------------|
| `TRIG_SW_PRESS(idx)` | 0x200 | data[0] == idx | data[1] == 0x01 |
| `TRIG_SW_RELEASE(idx)` | 0x200 | data[0] == idx | data[1] == 0x00 |
| `TRIG_SW_LONG(idx)` | 0x200 | data[0] == idx | data[1] == 0x02 |
| `TRIG_RELAY_BIT_ON(n)` | 0x101 | data[0] bit n set | — |
| `TRIG_RELAY_BIT_OFF(n)` | 0x101 | data[0] bit n clear | — |
| `TRIG_RELAY_CMD_ON(n)` | 0x100 | mask includes bit n | state bit n = 1 |
| `TRIG_RELAY_CMD_OFF(n)` | 0x100 | mask includes bit n | state bit n = 0 |
| `TRIG_ANY(id)` | id | — | — |

### Action reference

| Macro | Action | Args |
|-------|--------|------|
| `ACT_RELAY_TOGGLE(r)` | Toggle relay | r = relay index 0–5 |
| `ACT_RELAY_ON(r)` | Relay on | r = relay index 0–5 |
| `ACT_RELAY_OFF(r)` | Relay off | r = relay index 0–5 |
| `ACT_ALL_OFF()` | All relays off | — |
| `ACT_RELAY_SCENE(bmap)` | Set all relays to bitmap | bmap = 6-bit bitmap |
| `ACT_LED_ON(node, led)` | LED on | node = target node ID, led = LED index 0-based |
| `ACT_LED_OFF(node, led)` | LED off | same |
| `ACT_WIFI_ENABLE(node)` | Enable WiFi on node | node = target node ID |
| `ACT_WIFI_DISABLE(node)` | Disable WiFi on node | node = target node ID |
| `ACT_VIPER(cmd)` | Viper command | cmd = 0x01 lock, 0x02 unlock, 0x03 start |
| `ACT_MENU_SELECT()` | Menu navigate / confirm | — |
| `ACT_MENU_ENTER()` | Menu enter / execute | — |

### Example rules (in RULES_DEFAULT_INIT syntax)

```c
// SW1 toggles relay 1
RULE(TRIG_SW_PRESS(0), ACT_RELAY_TOGGLE(0))

// SW5 hold-style (horn) — on while held, off on release
RULE(TRIG_SW_PRESS(4),   ACT_RELAY_ON(4))
RULE(TRIG_SW_RELEASE(4), ACT_RELAY_OFF(4))

// BTN2 kills everything
RULE(TRIG_SW_PRESS(7), ACT_ALL_OFF())

// BTN1 navigates the LCD menu
RULE(TRIG_SW_PRESS(6), ACT_MENU_SELECT())
RULE(TRIG_SW_LONG(6),  ACT_MENU_ENTER())

// LED 1 mirrors relay 1 state (fires at 5 Hz while condition holds)
RULE(TRIG_RELAY_BIT_ON(0),  ACT_LED_ON(0x01, 0))
RULE(TRIG_RELAY_BIT_OFF(0), ACT_LED_OFF(0x01, 0))

// Any relay 2 CMD turning it on also arms the Viper
RULE(TRIG_RELAY_CMD_ON(1), ACT_VIPER(0x01))

// Set a scene: relays 1, 3, 5 on when SW3 is long-pressed
RULE(TRIG_SW_LONG(2), ACT_RELAY_SCENE(0x15))
```

---

## Web Console Aliases

These shortcuts expand to the raw CAN frames above. Enter them in the web console command line.

| Alias | Equivalent frame |
|-------|-----------------|
| `:relay 1 on` | `100 01 01` |
| `:relay 1 off` | `100 01 00` |
| `:relay 6 on` | `100 20 20` |
| `:alloff` | `100 3F 00` |
| `:horn` | `100 10 10` |
| `:viper lock` | `510 01` |
| `:viper unlock` | `510 02` |
| `:viper start` | `510 03` |
| `:readcfg relay` | `401 02 20 FF` |
| `:save relay` | `403 02 01` |
| `:reset relay` | `403 02 03` |
| `:cfgrelay 4 maxon 10000 !` | `400 02 20 04 00 10 27 01` |

---

## Quick Examples

```
# Turn on headlights (relay 1), fog lights (relay 2)
100 03 03

# Turn off just the fog lights, leave headlights alone
100 02 00

# Arm the alarm and turn relay 3 on at the same time
510 01
100 04 04

# Flash relay 1 — on then off (send both frames in sequence)
100 01 01
100 01 00

# Write "HELLO" to LCD row 1
500 01 00 48 45 4C 4C 4F

# Disable WiFi on the viper interface to save power
400 03 30 00 00 00 00 00 00

# Re-enable it
400 03 30 00 00 01 00 00 00

# Query battery voltage — watch frame log for 0x300 response
# (relay controller sends it automatically at 1 Hz — no query needed)

# ECU: switch to carb mode, set target 14.7 AFR, reset trim
308 01 00 00 00
308 02 00 BE 05
308 04 00 00 00

# Request capability frames from all nodes (bridge uses these to build its panel)
0F3 FF
```

---

## MQTT Bridge

The bridge node (0x05) can publish all CAN frames to an MQTT broker when `MQTT_BROKER` is defined in `bridge.h`. No CAN frames are needed to control MQTT — it's configured at compile time.

**Published topic:** `{MQTT_TOPIC_PREFIX}/frames`

Each message is a JSON object:
```json
{"id": 256, "data": [1, 1], "source": "can"}
```

**Injection topic:** `{MQTT_TOPIC_PREFIX}/send`

Publish a JSON frame to inject it onto the CAN bus:
```json
{"id": 256, "data": [1, 1]}
```

This is equivalent to typing `100 01 01` in the web console. The bridge converts and calls `bus_tx()`, so the frame appears on both the wired bus and via ESP-NOW, and is logged in the web UI frame log.
