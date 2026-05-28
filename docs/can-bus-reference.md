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
| cardputer | 0x06 |

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

### Bus error / recovery — `0x0F4 BUS_ERROR`

Emitted on error transitions and on recovery. Sent over both transports so ESP-NOW carries it even when wired CAN has failed.

| Byte | Field | Notes |
|------|-------|-------|
| 0 | node_id | sender's ID |
| 1 | error_code | 0=recovery, 1=BUS_OFF, 2=ERROR_PASSIVE, 3=TX_FAIL, 4=RX_OVERFLOW |
| 2 | tx_err_cnt | TWAI TX error counter |
| 3 | rx_err_cnt | TWAI RX error counter |

Used by `TRIG_BUS_ERROR()` and `TRIG_CAN_OK()` in the rules engine.

### Reboot a node — `0x0F5 REBOOT_CMD`

Any node may send this to restart one or all nodes.

| Byte | Field | Notes |
|------|-------|-------|
| 0 | target_node_id | Node to restart. `0xFF` restarts all nodes. |

The target calls `ESP.restart()` when its node_id matches or the target is `0xFF`. On the switch_panel the LCD briefly shows "Rebooting..." before restart.

```
# Reboot the relay controller
0F5 02

# Reboot all nodes simultaneously
0F5 FF
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

## Buzzer Control

### Send a buzzer command — `0x104 BUZZER_CMD`

Payload: `[target_node_id, cmd, arg0]`

- **target** — node ID to address. `0xFF` = broadcast to all nodes with `ENABLE_BUZZER`.
- **cmd** — sequence or control command (see table below).
- **arg0** — optional argument (peer count for `BUZZER_SEQ_PEER`, mute flag for `BUZZER_CMD_MUTE`).

| cmd | Constant | Sound | arg0 |
|-----|----------|-------|------|
| `0x01` | `BUZZER_SEQ_ALERT` | 3 urgent 1047 Hz pulses (interrupts active sequence) | — |
| `0x02` | `BUZZER_SEQ_CAN_UP` | Rising three-note confirmation | — |
| `0x03` | `BUZZER_SEQ_CAN_DOWN` | Two falling warning pulses | — |
| `0x04` | `BUZZER_SEQ_STARTUP` | Boot jingle (C-major arpeggio) | — |
| `0x05` | `BUZZER_SEQ_PEER` | Pitch-ladder tone by peer count | peer count (0–5) |
| `0x06` | `BUZZER_SEQ_WIFI_CONNECT` | Ascending A5→C6 chime (queued) | — |
| `0x07` | `BUZZER_SEQ_WIFI_DISCONNECT` | Descending C6→A5 chime (queued) | — |
| `0x10` | `BUZZER_SEQ_RELAY_ON` | Rising relay-on chirp | — |
| `0x11` | `BUZZER_SEQ_RELAY_OFF` | Falling relay-off chirp | — |
| `0x12` | `BUZZER_SEQ_ALL_OFF` | Descending three-note sweep | — |
| `0x13` | `BUZZER_SEQ_FUEL_PUMP_OFF` | 4 alternating low/high pulses — fuel pump safety cut | — |
| `0x20` | `BUZZER_CMD_MUTE` | Mute / unmute | `1` = mute, `0` = unmute |

```
# Alert on switch_panel (node 0x01)
104 01 01 00

# Alert broadcast to all nodes
104 FF 01 00

# Play startup jingle on switch_panel
104 01 04 00

# Mute switch_panel buzzer
104 01 20 01

# Unmute switch_panel buzzer
104 01 20 00
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

### Switch ACK — `0x202 SWITCH_ACK`

Any non-originating node that receives a `SWITCH_EVENT` replies with this frame to clear the switch panel's retry timer for that event.

Payload: `[switch_id, event]` — mirrors the fields from the `SWITCH_EVENT` being acknowledged.

The switch panel registers each `SWITCH_EVENT` in a pending-ACK slot. If no ACK arrives within 80 ms the frame is retransmitted up to 3 times. After exhausting retries, `buzzer_alert()` fires and a `LED_CMD` flashes LED index 2 (the error indicator). Any node with `ENABLE_RULES` serves as an implicit ACK responder.

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

## Blob Transfer

The blob protocol transfers multi-byte values (up to 256 bytes per key) over CAN in 4-byte chunks. It is used by `mod_blob` to handle credential updates and similar payloads that don't fit in a single CAN frame.

### Write chunk — `0x410 BLOB_WRITE`

Payload: `[target, ns, key, chunk_idx, d0, d1, d2, d3]`

| Byte | Field | Notes |
|------|-------|-------|
| 0 | target | Destination node ID. `0xFF` = all nodes. |
| 1 | ns | Namespace byte (see below) |
| 2 | key | Key within the namespace |
| 3 | chunk_idx | Chunk index. Byte offset = chunk_idx × 4. |
| 4–7 | d0–d3 | Up to 4 data bytes for this chunk |

### Commit — `0x411 BLOB_COMMIT`

Payload: `[target, ns, key, len_lo, len_hi, flags]`

Finalizes a blob transfer. The receiver assembles all chunks received since the last commit and acts on the complete value.

| Byte | Field | Notes |
|------|-------|-------|
| 0 | target | Destination node ID. `0xFF` = all nodes. |
| 1 | ns | Namespace byte |
| 2 | key | Key within the namespace |
| 3–4 | len (uint16 LE) | Total byte length of the assembled blob |
| 5 | flags | `0x01` BLOB_FLAG_PERSIST — save to NVS; `0x02` BLOB_FLAG_REBOOT — restart after saving |

Self-echoed `BLOB_COMMIT` frames are ignored by the blob handler; the sender writes its own copy directly.

### Blob namespaces

| Namespace | Value | Keys |
|-----------|-------|------|
| `BLOB_NS_WIFI` | `0x01` | `0x01` SSID (string), `0x02` password (string), `0x03` PMK (16 bytes), `0x04` LMK (16 bytes) |
| `BLOB_NS_RULES` | `0x02` | rule slot index (0–MAX_RULES-1) = 12-byte `CanRule` blob; key `0xFE` = factory reset sentinel |

`BLOB_NS_WIFI` is handled by `mod_wifi_creds`. Broadcast with `BLOB_FLAG_PERSIST` then send `REBOOT_CMD 0xFF` to update credentials and restart all nodes simultaneously.

`BLOB_NS_RULES` is emitted automatically by the `/api/rules` HTTP endpoints (POST/DELETE/reset) so that every rule change appears in the CAN frame log. The commit callback on the local node applies the change directly (self-echoed commits are ignored by the blob handler). `BLOB_FLAG_PERSIST` is always set.

---

## Other Sensor Frames (read-only)

These frames are broadcast automatically by their respective modules — no query needed.

| ID | Frame | Payload summary |
|----|-------|----------------|
| `0x301` | `ENV_DATA` | `[temp_d1_lo, temp_d1_hi, humi_d1_lo, humi_d1_hi]` — 0.1 °C / 0.1 % from DHT22 |
| `0x302` | `IMU_DATA` | `[accel_x_lo, accel_x_hi, accel_y_lo, accel_y_hi, accel_z_lo, accel_z_hi]` — raw accelerometer |
| `0x303` | `SHAKE_EVENT` | `[magnitude, axis_mask]` — emitted by viper_interface on shake detection |
| `0x310` | `IGNITION_DATA` | `[coil_cv_lo, coil_cv_hi, ign_on]` — coil + voltage centivolts (int16 LE) + boolean from hysteresis thresholds. Broadcast by `mod_fuel_pump` on the relay node (coil ADC sampling lives inside that module). Heartbeat every ~1 s + immediate on every on/off edge. |
| `0x311` | `FUEL_PUMP_STATE` | `[state, mode, reason, gates_ok]` — fuel pump FSM transition events. state: 0=PRIME 1=ARMED 2=RUNNING. mode: FuelPumpMode bitmask (0=OFF, 1=RPM, 2=COIL, 3=BOTH). reason: 0=none 1=boot 2=prime_done 3=gate_pass 4=stall 5=mode_change 6=re_enable. gates_ok: bit 0 = RPM gate currently passing, bit 1 = COIL gate. |

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

### Bluetooth enable / disable — key `0x33`

Enable or disable the BLE radio. Saves `bt_en` to NVS and restarts the node to apply. No-op on nodes compiled without `ENABLE_BLUETOOTH`.

`arg` (data[4]): `0` = disable, `1` = enable.

```
# Disable BLE on relay_controller (0x02)
400 02 33 00 00 00 00 00 00

# Enable BLE on relay_controller
400 02 33 00 00 01 00 00 00

# Disable BLE on all nodes
400 FF 33 00 00 00 00 00 00
```

### Bluetooth advertising control — key `0x34`

Start or stop BLE advertising at runtime without restarting the node. An already-connected phone is unaffected; stopping advertising just prevents new connections.

`arg` (data[4]): `0` = stop advertising, `1` = start advertising.

```
# Stop advertising on relay_controller (prevent new BLE connections)
400 02 34 00 00 00 00 00 00

# Resume advertising
400 02 34 00 00 01 00 00 00
```

---

### Fuel pump safety mode — key `0x61`

Sets the multi-gate fuel pump safety mode at runtime. RAM-only (not persisted) — every reboot restores `FUEL_PUMP_DEFAULT_MODE`. Identical semantics to the `ACT_FUEL_PUMP_SAFETY_*` rule actions; both call `fuel_pump_set_mode()`. Relay-controller only.

`arg` (data[4]) is the `FuelPumpMode` bitmask:

| Value | Name | RPM gate | COIL gate |
|-------|------|----------|-----------|
| `0` | `FP_MODE_OFF` | disabled | disabled — pump forced ON, FSM frozen |
| `1` | `FP_MODE_RPM` | enabled | disabled |
| `2` | `FP_MODE_COIL` | disabled | enabled |
| `3` | `FP_MODE_BOTH` | enabled | enabled — default, strictest (AND) |

```
# Mode BOTH (strictest)
400 02 61 00 00 03 00 00 00

# Mode COIL only
400 02 61 00 00 02 00 00 00

# Mode RPM only
400 02 61 00 00 01 00 00 00

# Disable safety (pump forced on)
400 02 61 00 00 00 00 00 00
```

Mode changes emit a `FUEL_PUMP_STATE (0x311)` frame with reason=5 (mode_change). The FSM restarts from PRIME on any non-OFF transition so the carb bowl is primed before the next gate check.

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
| `TRIG_BOOT()` | 0x0F1 | — | — |
| `TRIG_BUS_ERROR()` | 0x0F4 | any error_code | — |
| `TRIG_CAN_OK()` | 0x0F4 | error_code == 0 (recovery) | — |
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
| `ACT_BUZZER_ALERT()` | 3 urgent pulses on local node | — |
| `ACT_BUZZER_PLAY(node, seq)` | Send `BUZZER_CMD` to target node | node = target node ID or 0xFF; seq = `BUZZER_SEQ_*` (see [Buzzer Control](#buzzer-control)) |
| `ACT_BUZZER_PLAY_ARG(node, seq, arg)` | Same with extra arg | arg = e.g. peer count for `BUZZER_SEQ_PEER` |
| `ACT_RELAY_TIMED_OFF(r, secs)` | Relay on then auto-off after timeout | r = relay index 0–5; secs = delay in seconds (1–255) |
| `ACT_LED_FLASH(node, led, period_ds)` | Flash an LED | node = target node ID; led = LED index; period_ds = period in 100 ms units |
| `ACT_FUEL_PUMP_SAFETY(mode)` | Set fuel pump safety mode (relay-controller only) | mode = FuelPumpMode bitmask: 0=OFF, 1=RPM, 2=COIL, 3=BOTH |
| `ACT_FUEL_PUMP_SAFETY_DISABLE` | Force pump ON, freeze FSM | — |
| `ACT_FUEL_PUMP_SAFETY_RPM_ONLY` | RPM gate only | — |
| `ACT_FUEL_PUMP_SAFETY_COIL_ONLY` | COIL gate only | — |
| `ACT_FUEL_PUMP_SAFETY_BOTH` | RPM AND COIL (strictest) | — |
| `ACT_FUEL_PUMP_SAFETY_ENABLE` | Alias for RPM_ONLY (back-compat) | — |

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

## WiFi Credentials

WiFi and ESP-NOW credentials (SSID, password, PMK, LMK) are stored in NVS by `mod_wifi_creds` (namespace `"wifi_creds"`). On first boot, or after a factory reset, they are seeded from compile-time `secrets.h` values. Credentials can be updated at runtime without reflashing.

### REST API (web console only, not CAN)

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/wifi_creds` | Return current credentials as `{ssid, pass, pmk_hex, lmk_hex}` |
| POST | `/api/wifi_creds` | Update credentials. Body: JSON with any subset of `ssid`, `pass`, `pmk_hex`, `lmk_hex`. Include `"broadcast": true` to blob-broadcast the new values to all nodes. |
| POST | `/api/wifi_creds/reset` | Restore `secrets.h` compile-time defaults in RAM and NVS |

Changes take effect on the next reboot. After a credential broadcast, send `REBOOT_CMD 0xFF` (or use the "Reboot All" button in the Settings panel) to restart all nodes with the new credentials simultaneously.

### Over CAN (blob transfer)

Credentials are transferred node-to-node via `BLOB_WRITE (0x410)` + `BLOB_COMMIT (0x411)` with `BLOB_NS_WIFI (0x01)`. See [Blob Transfer](#blob-transfer) above.

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
