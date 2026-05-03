# Module: mod_mqtt

MQTT bridge for the bridge node. Publishes every CAN frame to a configurable topic and subscribes to a send topic for frame injection. Runs on the bridge node alongside `BRIDGE_MODE`.

## Enable

```cpp
#define MQTT_BROKER "192.168.1.100"   // broker IP or hostname
```

Defining `MQTT_BROKER` enables the module. If undefined, mod_mqtt compiles to nothing.

Requires the **PubSubClient** library:
```bash
arduino-cli lib install "PubSubClient"
```

---

## Config defines

| Define | Default | Description |
|--------|---------|-------------|
| `MQTT_BROKER` | — | Broker IP or hostname. **Required.** Setting this enables the module. |
| `MQTT_PORT` | `1883` | Broker TCP port |
| `MQTT_TOPIC_PREFIX` | `"canbus"` | Base topic string. All topics are prefixed with this. |
| `MQTT_CLIENT_ID` | `"canoe-bridge"` | MQTT client identifier |

---

## Topics

### Published — `{prefix}/frames`

Published for every CAN frame observed on the bus (both sent and received). Payload is JSON:

```json
{"id": 256, "data": [1, 1], "source": "can"}
```

| Field | Description |
|-------|-------------|
| `id` | CAN frame ID (decimal) |
| `data` | Payload bytes as a JSON array (up to 8 bytes) |
| `source` | `"can"`, `"wifi"`, or `"self"` |

Examples:
```json
{"id": 256, "data": [1, 1], "source": "can"}        relay 1 ON command (0x100)
{"id": 257, "data": [1], "source": "can"}            relay status: relay 1 on (0x101)
{"id": 512, "data": [0, 1], "source": "can"}         switch 0 pressed (0x200)
{"id": 775, "data": [1, 90, 15, 120, 65, 128, 12, 9], "source": "can"}   ECU_DATA (0x307)
```

### Subscribed — `{prefix}/send`

Publish here to inject a frame onto the CAN bus. The bridge calls `bus_tx()`, so the frame appears on the wired CAN bus, via ESP-NOW to all peers, and in the web UI frame log.

Payload format:
```json
{"id": 256, "data": [1, 1]}
```

```json
{"id": 256, "data": [63, 0]}          all relays OFF  (100 3F 00)
{"id": 257, "data": [1, 1]}           relay 1 ON  (not needed — use 0x100)
{"id": 1296, "data": [1, 1, 0, 0]}    ECU_CMD: set mode = injection  (308 01 01 00 00)
```

---

## Integration notes

- `mqtt_setup()` must be called in the bridge node's `setup()` after WiFi STA is connected.
- `mqtt_tick()` must be called every `loop()` — it maintains the MQTT connection and processes the incoming subscribe queue. If the broker disconnects, `mqtt_tick()` reconnects automatically.
- `mqtt_handle_frame()` must be called for every frame returned by `bus_rx()` — it publishes the frame to `{prefix}/frames`.
- The bridge node does not run ESP-NOW (`BRIDGE_MODE` disables it). Frames published via `{prefix}/send` reach the bus via the wired CAN transceiver only.
- Frame rate can be high — at 5 Hz RELAY_STATUS plus NODE_ANNOUNCE every 5 s plus any active switch events, a busy bus can produce dozens of MQTT messages per second. Consider filtering on the broker side if needed.
