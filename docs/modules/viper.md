# Module: mod_viper

Viper 5305V serial bridge. Receives lock/unlock/remote-start commands from the CAN bus, translates them to the Viper's 5-byte serial protocol over UART2, and broadcasts the alarm's raw response back onto the bus.

## Enable

```cpp
#define ENABLE_VIPER
```

---

## Config defines

| Define | Example | Description |
|--------|---------|-------------|
| `VIPER_TX_PIN` | `16` | UART2 TX → level shifter LV → HV → Viper serial RX |
| `VIPER_RX_PIN` | `17` | UART2 RX ← level shifter LV ← HV ← Viper serial TX |

The Viper communicates at 9600 baud, 8N1, **5 V TTL**. A bidirectional level shifter is required (LV = 3.3V, HV = 5V, shared GND with Viper).

---

## CAN frames

### Receives

| ID | Name | Payload | Behaviour |
|----|------|---------|-----------|
| `0x510` | `VIPER_CMD` | `[cmd]` | Sends the corresponding 5-byte serial command to the Viper module. |

#### VIPER_CMD commands

| `data[0]` | Constant | Action |
|-----------|----------|--------|
| `0x01` | `VIPER_CMD_LOCK` | Lock / arm alarm |
| `0x02` | `VIPER_CMD_UNLOCK` | Unlock / disarm alarm |
| `0x03` | `VIPER_CMD_REMOTE_START` | Remote start engine |

### Sends

| ID | Name | Payload | Triggered by |
|----|------|---------|--------------|
| `0x511` | `VIPER_STATUS` | `[b0, b1, b2, b3, b4]` | Each time the Viper alarm sends back a 5-byte response packet. |

---

## Usage examples

```
510 01    lock / arm
510 02    unlock / disarm
510 03    remote start
```

Or via web console aliases:
```
:viper lock
:viper unlock
:viper start
```

---

## Integration notes

- `viper_handle_frame()` must be called for every frame returned by `bus_rx()`.
- `viper_loop()` must be called every `loop()` — it drives `ViperESP2::update()` which collects incoming bytes from the alarm and fires the response callback.
- GPIO 16 / 17 are UART2 on the viper_interface ESP32 and relay outputs on the relay_controller ESP32. These are different physical boards — no conflict.
- To reverse-engineer the Viper's serial byte order on your specific module revision, temporarily call `ViperESP2::sniff()` from `loop()` instead of `update()` and watch the serial monitor for raw received bytes.
- `VIPER_STATUS` broadcasts the raw 5-byte alarm packet. The meaning of each byte varies by Viper firmware version; refer to Viper's serial protocol documentation for your module revision.
