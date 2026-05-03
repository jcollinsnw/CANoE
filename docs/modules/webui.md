# Module: webui

SoftAP, captive portal, HTTP server, and JSON API. Always compiled in — every node runs a web console. The bridge node additionally connects to a home router as a WiFi STA.

## Enable

Always active. No feature flag. Configure via defines in the node config header.

---

## Config defines

### SoftAP

| Define | Default | Description |
|--------|---------|-------------|
| `AP_SSID` | `"AccessoryBus"` | WiFi network name broadcast by this node |
| `AP_PASSWORD` | `""` (open) | WPA2 password. Must be ≥ 8 characters if set. Must match on all nodes. |
| `AP_HIDDEN` | `0` | Set to `1` to hide the SSID from scan results |

All nodes broadcast on channel 6 (fixed). This ensures ESP-NOW peers find each other regardless of which node the user's phone is associated with.

### Bridge node only

| Define | Required | Description |
|--------|----------|-------------|
| `STA_SSID` | Yes | Home router SSID for STA connection |
| `STA_PASSWORD` | Yes | Home router password |

`BRIDGE_MODE` (in `configs/bridge.h`) activates the STA connection. The bridge connects to `STA_SSID` on startup and retries in `webui_tick()` if it drops. The SoftAP and web server remain available while STA is connecting.

---

## HTTP API endpoints

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/` | Single-page web UI (embedded from `index_html.h`) |
| `GET` | `/api/frames` | JSON array of recent CAN frames (ring buffer) |
| `POST` | `/api/tx` | Inject a CAN frame: `{"id": 256, "data": [1, 1]}` |
| `POST` | `/api/tx_mode` | Toggle force-WiFi-only: `{"wifi_only": true}` |
| `GET` | `/api/config` | Node capability JSON (see below) |
| `GET` | `/api/nodecaps` | Array of NODE_CAP frames seen (bridge and all WiFi nodes) |
| `GET` | `/api/rules` | All rule slots as a JSON array |
| `POST` | `/api/rules` | Upsert a rule slot (body: JSON with all CanRule fields + `"i"` index) |
| `DELETE` | `/api/rules?i=N` | Clear rule slot N |
| `POST` | `/api/rules/reset` | Restore compiled-in `RULES_DEFAULT_INIT` defaults |

### `/api/config` response

```json
{
  "has_relay": false,
  "has_switches": true,
  "has_viper": false,
  "has_rules": true,
  "has_leds": true,
  "is_bridge": false,
  "switch_count": 6,
  "button_count": 4,
  "led_count": 3,
  "relay_count": 0,
  "relay_labels": ["", "", "", "", "", ""]
}
```

The Control tab fetches this on load to decide which UI sections to render.

### `/api/nodecaps` response

```json
[
  {
    "id": 1,
    "caps": 26,
    "switch_count": 6,
    "button_count": 4,
    "led_count": 3,
    "relay_count": 0,
    "age_ms": 4200
  },
  {
    "id": 2,
    "caps": 1,
    "switch_count": 0,
    "button_count": 0,
    "led_count": 0,
    "relay_count": 6,
    "age_ms": 1100
  }
]
```

`age_ms` is milliseconds since the last `NODE_CAP (0x0F2)` frame was received from that node. The bridge web UI uses this to build its aggregated control panel.

---

## CAN frames

### Observed (not sent or received by webui directly)

`webui_observe()` is registered as the bus observer via `bus_set_observer()`. Every frame that passes through `bus_tx()` or `bus_rx()` is forwarded to the web UI frame log. The web UI sees all frames including self-echoed ones (`source="self"`).

`webui_handle_node_cap()` must be called for every `NODE_CAP (0x0F2)` frame to keep the `/api/nodecaps` cache current.

---

## Serial tee

`wlog()` and `wlogln()` replace `Serial.printf()` and `Serial.println()` throughout the firmware. They write to both UART and the web UI's **Serial** tab simultaneously.

```cpp
wlog("[relay] relay %d ON\n", idx);
wlogln("[bus] CAN OK");
```

`webui_serial_tee_install()` hooks into the Arduino `Serial` stream so that any third-party library calling `Serial.print()` also appears in the web UI (optional, bridge only).

`mod_serial_shell` bypasses this tee and writes to `Serial` directly — shell output is UART-only.

---

## Captive portal

A DNS server on port 53 answers all queries with the node's AP IP (`192.168.4.1`). The HTTP server catches common captive-portal probe paths used by iOS and Android and redirects them to `/`. This causes most devices to auto-open the web console when joining the `AccessoryBus` network.

---

## Integration notes

- `webui_init()` must be called before `bus_init()`. It brings up WiFi (SoftAP + optional STA), which must be active before `esp_now_init()` in `bus_init()` can bind to a radio.
- `webui_tick()` must be called every `loop()`. It processes DNS queries, HTTP requests, and (on bridge nodes) manages the STA reconnection state machine.
- `webui_observe()` is installed automatically by `webui_init()` via `bus_set_observer()`. Do not call it directly.
- `webui_handle_node_cap()` must be called explicitly in the frame dispatch loop for `NODE_CAP (0x0F2)` frames. See `accessory_node.ino` for the call site.
- **Security:** The AP is open by default — acceptable in a private garage, risky in public. Set `AP_PASSWORD` in each node's config header before any field install. The password must match on all nodes and be ≥ 8 characters for WPA2. Also consider `AP_HIDDEN 1` for production.
- **Bridge STA:** The bridge does not run ESP-NOW (`BRIDGE_MODE` disables it). Frames published via MQTT or the bridge's web UI reach the wired CAN bus via the TWAI transceiver only — they do not propagate to other nodes over ESP-NOW.
