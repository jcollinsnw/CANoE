# CANoE — Project Roadmap

Future features, integrations, and platform evolution ideas. Items are grouped by theme, not strict priority — the order within each section is rough.

---

## Security & Reliability

- ~~**Encrypt ESP-NOW**~~ — done: PMK set at init; dynamic encrypted unicast peers added on first contact; broadcast retained for discovery with dedup handling overlap.
- ~~**ESP-NOW ack/retry for switch events**~~ — done: `CAN_ID_SWITCH_ACK (0x202)`, 3 retries at 80 ms, `buzzer_alert()` + LED flash on exhaustion.
- **Low-voltage cutoff** — `mod_relay.cpp`: when `vbat_cv` from `TELEMETRY (0x300)` drops below a configurable threshold (stored in NVS, editable over CAN), force non-essential relays off and broadcast an alert. Protects the battery from being drained to failure.
- **Battery voltage graph** — Control tab in the web UI already receives `TELEMETRY (0x300)` frames; add a small rolling graph of `vbat_cv` over time.

---

## Developer Tooling

- **Python `can-gw` utility** — CLI tool for Mac (or any SocketCAN machine) using a CANable/MCP2515 USB adapter. Should support: live frame log with human-readable decoding of CANoE IDs, frame injection by alias (`:relay 1 on`), rule export/import, and CSV logging. Built on `python-can` + the existing JSON API.
- **SSE or WebSocket frame log** — replace the current polling approach in the web UI with a server-sent event stream. Lower latency, less traffic, cleaner reconnect behavior.
- **Bus speed upgrade path** — document and test bumping `CAN_BUS_SPEED` from 125 to 500 kbit/s across all nodes simultaneously. Required prerequisite for most CAN ecosystem integrations below.

---

## CAN Ecosystem — Sensors

Standard CAN-output sensors eliminate analog wiring, ADC calibration drift, and noise from the engine bay. Each becomes a new `mod_` that subscribes to the sensor's documented frame(s) and re-publishes as internal CANoE frames — the rest of the system (LCD, rules, web UI) sees no difference.

- **AEM X-Series Wideband via CAN** — replaces the analog `mod_wbo2` ADC path. The AEM unit broadcasts lambda and AFR on a configurable 11-bit ID at 500 kbit/s. No calibration resistors, no 0–5V noise. `WBO2_DATA (0x306)` frame content stays identical.
- **CAN pressure / temperature sensors** — oil pressure, fuel pressure, coolant temp, oil temp as bus participants instead of analog inputs. AEM, Bosch Motorsport, and Continental all make CAN-output variants of common sensors.
- **Transmission controller CAN interface** — TCI, Turbo Action, and similar aftermarket transmisison controllers expose CAN ports for gear position, TCC lockup status, line pressure. Feed into rules engine: e.g. disable launch control relay above 2nd gear.
- **CAN-output GPS** — higher-accuracy alternative to `mod_gps` NMEA UART parsing; many u-blox and SkyTraq modules support CAN output directly.

---

## CAN Ecosystem — ECUs

- **Aftermarket ECU translation module** (`mod_ecu_can`) — a config-driven translator that maps an aftermarket ECU's broadcast frames to CANoE internal frames. Target ECUs: Haltech Elite, Link G4X, AEM Infinity, MegaSquirt MS3 CAN expansion. Each ECU has a documented (or configurable) frame map; the module subscribes to those IDs and re-publishes as `ENGINE_DATA (0x304)`, `ECU_DATA (0x307)`, `WBO2_DATA (0x306)`. Replaces `mod_ecu.cpp` when running an external ECU.
- **OBD-II poller module** (`mod_obd2`) — acts as an ISO 15765-4 tester: sends mode 0x01 PID requests on a configurable schedule, parses single-frame responses (most sensor PIDs), re-publishes on internal CANoE frames. Requires being on the same physical bus as the OBD-II ECU (typically 500 kbit/s). Primarily useful with modern crate engine controllers; antique Mustang factory wiring pre-dates CAN OBD.
- **PDM (Power Distribution Module) support** — high-end PDMs from Motec, Haltech, and AiM replace the relay + fuse box with a solid-state unit controlled entirely via CAN. `RELAY_CMD (0x100)` frames would be translated to the PDM's protocol by a bridge module; the PDM reports back load current and fault status per channel. Long-term hardware evolution path for the relay controller node.

---

## CAN Ecosystem — Displays

- **Digital dash CAN output** — AiM MXS/MXG, Holley EFI Dash, and Racepak units receive CAN data for display. Publish `ENGINE_DATA`, `ECU_DATA`, `WBO2_DATA`, `TELEMETRY`, and `RELAY_STATUS` on the IDs the dash expects (or use the dash's configurable CAN stream mapping). Eliminates separate sensor wiring to the dash.

---

## Bus Architecture

- **Dual-bus bridge node** — a node with two CAN interfaces (ESP32 TWAI + MCP2515 SPI) that sits between the CANoE accessory bus and a second bus running a different protocol or speed (e.g. 500 kbit/s aftermarket ECU bus, or a factory body-control network). Selectively translates frames in both directions. Required when two bus segments cannot be merged due to speed mismatch or isolation requirements.
- **Auto-unify with factory wiring** — long-term: firmware recognizes a `factory_wiring_trusted` flag in NVS. When set, the relay controller bridges accessory loads back onto the factory harness rather than running parallel. Only safe once the factory wiring has been fully inspected and load-tested.

---

## Tuning Platform — Quasar Web App

A full off-device tuning platform built as a Quasar/Vue app that talks to the existing ESP32 JSON API. Keeps the on-device web UI minimal and offline-capable while the desktop/tablet interface gets a rich feature set. Designed to run on a tablet mounted in the car or a laptop in the garage.

**Real-time monitoring**
- Live gauges for RPM, AFR, MAP, TPS, coolant temp, IAT, battery voltage, GPS speed
- Rolling time-series graphs with configurable time window and per-channel enable/disable
- CAN frame log with full CANoE protocol decoding, filtering by ID, and hex/decoded toggle

**ECU tuning**
- VE (volumetric efficiency) table editor — 2D grid with RPM × MAP axes, cell-level edit and interpolation preview
- AFR target table editor — RPM × MAP, with overlay showing current measured AFR vs target
- Short-term fuel trim (STFT) live display and reset
- Base pulse width (injection mode) and PWM duty (carb mode) configuration
- Closed-loop PID gain tuning with live response graph
- Mode switch (carb ↔ TBI injection) with confirmation dialog

**Data logging**
- Session recording to CSV: all broadcast frames decoded to engineering units
- Playback / overlay — replay a logged session and compare against a new run on the same graph
- Lap/run timer tied to GPS speed signal

**Rules & configuration**
- Enhanced rules editor with visual trigger/action builder (richer than the current in-device tab)
- Rule import/export as JSON
- Node configuration panel: relay labels, max-on timeouts, LED assignments, bus speed

**Diagnostics**
- OBD-II PID request UI (when `mod_obd2` is active)
- CAN bus health dashboard: error counters, BUS_ERROR event history, transport status per node
- NVS key inspector: read/write any config key on any node via `CONFIG_READ_REQ` / `CONFIG_WRITE`

---

## Done

- ~~SoftAP password~~ — set on all nodes (REDACTED / REDACTED)
- ~~Dashboard Control tab~~ — switch state, relay tiles, LED dots, Viper controls
- ~~Bridge node WiFi STA + MQTT~~ — `bridge.h`, `BRIDGE_MODE`, `mod_mqtt`
- ~~Horn timeout → rules engine~~ — `ACT_RELAY_TIMED_OFF` rule, editable at runtime
- ~~LED flash mode via CAN~~ — 4-byte `LED_CMD` extension, `ACT_LED_FLASH` rule action
- ~~CAN reliability monitoring~~ — `CAN_ID_BUS_ERROR`, error-passive detection, alert rules
- ~~LCD startup animation + jingle~~ — CANoE reveal, mutable via menu
- ~~BLE GATT CAN mirror~~ — `mod_bluetooth` on ESP32 + Tuner iOS app; auto-pair, background GPS injection, live frame log
- ~~Reboot menu item~~ — `MENU_HAS_REBOOT`; sends `REBOOT_CMD (0x0F5)` to any node by ID; LCD shows "Rebooting..." on local reboot
- ~~SoftAP client notifications~~ — `webui_set_ap_client_cb()`; plays buzzer chime + shows LCD event on connect/disconnect
- ~~Runtime WiFi credential management~~ — `mod_wifi_creds` + `mod_blob`; credentials stored in NVS, seeded from `secrets.h` on first boot; generic chunked blob transfer protocol (`BLOB_WRITE 0x410` / `BLOB_COMMIT 0x411`); broadcast to all nodes via blob + `REBOOT_CMD 0xFF`; WiFi Credentials panel in Settings tab
- ~~ESP-NOW key rotation~~ — `bus_set_espnow_keys()` allows NVS-loaded PMK/LMK to override compile-time `secrets.h` at runtime


## Quick TODOs (TO ORGANIZE)
- ~~Buzzer CAN?~~ — done: `CAN_ID_BUZZER_CMD (0x104)` with `BUZZER_SEQ_*` / `BUZZER_CMD_MUTE`; `RULE_ACT_BUZZER_PLAY` sends frame through CAN; `buzzer_handle_frame()` responds to target or broadcast.
- ~~Web UI communication audit~~ — done: rules POST/DELETE/reset now emit `BLOB_NS_RULES (0x02)` frames visible in CAN log; all writes go through the bus.
- LCD Menu for Bluetooth discoverability/power
- Switch wiring to regular rotary encoder