# CANoE — Project Roadmap

Future features, integrations, and platform evolution ideas. Items are grouped by theme, not strict priority — the order within each section is rough.

---

## Security & Reliability

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

- **LSU 4.9 wideband hardware** — off-brand LSU 4.9 sensor + standalone controller. `mod_wbo2` already handles the 0–5V analog output via a 100 kΩ+100 kΩ divider; oversampling and automatic 11 dB ADC attenuation added. Wire up and calibrate `WBO2_MIN/MAX_AFR` against stoich once installed.
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

## Unorganized

- Switch wiring to regular rotary encoder

---

## iOS Tuner App

- **Engine data logging** — record sessions to CSV on the phone: parse all sensor data including `ENGINE_DATA (0x304)`, `ECU_DATA (0x307)`, `WBO2_DATA (0x306)`, `TELEMETRY (0x300)`, and weather data frames into timestamped rows. Display rolling time-series graphs per channel with a configurable time window. Export via the iOS share sheet. The UI should be customizable with the ability to add a graph, add multiple data series's to that graph (telemetry, engine data, etc). Implement standard data logging functions that are common with OBD2 scanners.

---

## Done

- ~~LCD menu Bluetooth submenu~~ — `MENU_HAS_BLUETOOTH`; Advertise toggle (runtime) + BT Power toggle (NVS + restart)
- ~~RPM sensor on the relay controller~~ — `ENABLE_RPM` on relay node; GPIO 35 with external 10 kΩ pull-up; advertises `CHAN_ID_RPM` alongside VBAT channels
- ~~Multi-gate fuel pump safety + coil voltage sense~~ — `mod_fuel_pump` FSM with PRIME / ARMED / RUNNING states. Gates: RPM (from `ENGINE_DATA`) and COIL (from on-board ADC sampling of GPIO 39 with hysteresis, 5.545:1 divider, 6.00 V on / 4.00 V off). Mode bitmask (`FP_MODE_OFF / RPM / COIL / BOTH`) settable via `CFG_KEY_FUEL_PUMP_SAFETY (0x61)` or `ACT_FUEL_PUMP_SAFETY_*` rule actions. On stall, broadcasts `BUZZER_SEQ_FUEL_PUMP_OFF` so the switch panel buzzer and Cardputer both alert audibly. The coil sensing was originally a separate `mod_ignition` module; merged into `mod_fuel_pump` once it was clear they were never used independently. `IGNITION_DATA (0x310)` is still broadcast for observability.
- ~~Cardputer firmware OTA upload~~ — `ENABLE_OTA_UPLOAD` adds an OTA screen that lists `.bin` files from `/canoe-firmwares/` on the MicroSD and uploads to `http://192.168.4.1/api/ota`. ESP-NOW stays alive on channel 6 throughout (STA association only, no mode change). Limitation: all nodes share the SSID so only one target should be powered on during upload — see [CLAUDE.md roadmap items 10–11](CLAUDE.md) for per-node SSID disambiguation and MD5 verification follow-ups.
