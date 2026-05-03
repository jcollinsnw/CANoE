# Module: bus

Dual-transport abstraction layer. All application code sends and receives frames through `bus_tx()` / `bus_rx()` — never directly via `twai_transmit` or `esp_now_send`. Handles TWAI (wired CAN) and ESP-NOW (WiFi fallback) simultaneously, with deduplication, self-echo, and automatic recovery.

## Enable

Always compiled in. No flag required.

---

## Config defines

| Define | Default | Description |
|--------|---------|-------------|
| `USE_CAN_TRANSCEIVER` | — | **Required.** `0` = bench mode (open-drain TX, NO_ACK), `1` = production (push-pull to TJA1051/SN65HVD230) |
| `USE_WIFI` | — | **Required.** `1` = enable SoftAP + ESP-NOW. `0` = CAN-only |
| `NODE_ID` | — | **Required.** This node's CAN address (0x01–0xFE) |

---

## CAN frames

`bus` is a transport layer, not a protocol participant. It does not own any CAN IDs. However it generates two infrastructure frames:

### Sends (infrastructure)

| ID | Name | Payload | Rate |
|----|------|---------|------|
| `0x0F0` | `NODE_ANNOUNCE` | `[node_id, peer_count, can_ok]` | Every 5 s |
| `0x0F2` | `NODE_CAP` | `[node_id, caps, sw_count, btn_count, led_count, relay_count]` | Boot + every 30 s, and on `NODE_CAP_REQ` |

### Receives

| ID | Name | Handled |
|----|------|---------|
| `0x0F3` | `NODE_CAP_REQ` | Responds immediately with `NODE_CAP` if `data[0]` matches this node or is `0xFF` |

---

## Self-echo

`bus_tx()` feeds every outbound frame back into the RX ring (tagged `source="self"`). This means modules on the same node react to their own transmitted frames — relay status, buzzer cues, rules evaluation, and LCD updates all fire for locally-generated frames exactly as they would for frames from remote nodes.

---

## Deduplication

ESP-NOW frames carry a `(node_id, seq)` header. A 2-second sliding window suppresses duplicates. TWAI frames are always passed through (no dedup needed — the wired bus is reliable).

---

## TX modes

| Mode | Behaviour |
|------|-----------|
| `CAN_WIFI` (default) | Transmits on both TWAI and ESP-NOW |
| `WIFI_ONLY` | Skips TWAI TX — used by "force WiFi-only" toggle in web UI |
| `CAN_ONLY` | Skips ESP-NOW TX |

Change at runtime: `bus_set_tx_mode(BusTxMode)`.

---

## Bench mode wiring

Set `USE_CAN_TRANSCEIVER 0`. After `twai_driver_install()`, the firmware sets `GPIO.pin[5].pad_driver = 1` to make GPIO 5 open-drain **without** disconnecting the TWAI peripheral from the GPIO matrix. TWAI runs in `NO_ACK` mode.

> Do **not** use `gpio_set_direction(GPIO_NUM_5, GPIO_MODE_OUTPUT_OD)` — it re-routes the GPIO matrix and breaks the TWAI peripheral binding.

All nodes on the bench wire must have `USE_CAN_TRANSCEIVER 0`. A mixed bench/production bus causes errors on every frame.

---

## Integration (setup order)

```cpp
// In setup() — order is fixed:
relay_setup();       // outputs known-good before any CAN traffic
setup_can();         // not needed — bus_init handles TWAI
webui_init(...);     // brings WiFi up so ESP-NOW has a radio
bus_init(NODE_ID);   // starts TWAI + ESP-NOW, registers peers
```

`bus_init` must be called **after** `webui_init` because ESP-NOW requires the WiFi radio to be initialised first.

---

## Key API

```cpp
void  bus_init(uint8_t node_id);
void  bus_tx(uint16_t id, const uint8_t* data, uint8_t dlc);
bool  bus_rx(BusFrame& out);          // returns true if frame available
void  bus_tick();                      // call once per loop()

bool  bus_can_healthy();
bool  bus_wifi_seen_peer();
uint8_t bus_peer_count();

void  bus_set_tx_mode(BusTxMode mode);
```

`BusFrame` fields: `id`, `dlc`, `data[8]`, `source[]` (`"can"`, `"wifi"`, or `"self"`).
