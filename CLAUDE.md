# Antique Car CAN Bus Accessory System

A parallel 12V accessory wiring system for an antique car, built around multiple ESP32 nodes on a shared CAN bus with ESP-NOW wireless fallback and a self-hosted web console. This file exists to onboard future Claude sessions quickly — read it top to bottom before making changes.

## Project goals (from the owner)

- Keep the factory wiring completely isolated while a parallel accessory system runs on its own battery, fuse box, and charging path.
- Down the road, unify the two systems once the factory wiring is vetted.
- All accessory loads driven through a 6-relay / 6-fuse box.
- CAN bus between a switch panel (buttons/toggles) and the relay controller.
- CAN-frame-triggered rules engine for flexible switch→relay/LED/alarm mappings, stored in NVS and editable at runtime.
- Web console for debugging, direct CAN frame injection, and rules management.
- WiFi fallback if the CAN wire ever fails.

## Power architecture (important context)

The car uses a **chassis-ground master switch** as the system kill — a single switch that breaks the battery's ground path so the entire car (factory + accessory) is electrically disconnected when off. There is no key-controlled relay between the accessory battery and the relay node; **the relay node is powered continuously whenever the master switch is on**, regardless of whether the key is in the ignition.

That means "ESP32 boot" does **not** correspond to "driver got in the car." It corresponds to "user flipped the master switch on" — which might be hours before the user actually drives. Anything that should only run while the engine is alive (e.g. fuel pump) needs a separate, evidence-based gate; **boot is not a proxy for "engine running."** The original OEM fuel pump burned out because the boot rule turned R1 on, the user parked at a restaurant with the master switch still on (and the key out), and the pump dead-headed against a closed float valve for hours. The `mod_fuel_pump` FSM exists to prevent that — see the gotcha entry below.

Signals available to detect "engine alive":
- **Ignition coil voltage** (`mod_fuel_pump`, GPIO 39) — coil + side is fed via the dash ballast resistor; ~9 V key-in-RUN, ~12 V cranking, 0 V key-out. This is the most direct "is the key in the car" signal. The coil voltage ADC sampling lives inside `mod_fuel_pump` (folded in from the former `mod_ignition`); it still broadcasts `IGNITION_DATA (0x310)` for observability but the FSM consumes it directly without the CAN round trip.
- **RPM pulses** (`mod_rpm`, GPIO 35) — PC817C optocoupler on the coil(–) side; presence of pulses proves the engine is actually turning.

## Repo layout

```
.
├── CLAUDE.md                                   # this file
├── Makefile                                    # configure + compile/upload
├── wiring-with-transceivers.svg                # production wiring diagram
├── wiring-bench-mode.svg                       # bench wiring (no transceivers)
├── tuner/                                      # native iOS companion app (see below)
│   ├── README.md
│   ├── Tuner.xcodeproj/
│   └── Tuner/
│       ├── TunerApp.swift                      # @main — injects AppSettings environment object
│       ├── Theme.swift                         # Color.acapulcoBlue, AppSettings (font scales), AppearanceSheet
│       ├── ContentView.swift                   # root view: ConnectView / NativeDashboardView / NodeBrowserView
│       ├── LogView.swift                       # data logger UI: live gauges, line graphs, Braun analog dials
│       ├── DataLogger.swift                    # session recording, CSV export, channel descriptors
│       ├── BLEManager.swift                    # CoreBluetooth central, auto-reconnect, frame log
│       ├── LocationManager.swift               # CoreLocation → GPS_DATA (0x305) CAN frames
│       ├── NodeWebView.swift                   # WKWebView with pull-to-refresh + JS GPS bridge
│       ├── SimulatorDriver.swift               # synthetic engine data for testing without hardware
│       ├── WeatherService.swift                # ambient weather fetch → ENV_DATA (0x301)
│       └── AltimeterManager.swift              # barometric pressure from CMAltimeter
└── firmware/
    ├── configs/                                # one header per physical node
    │   ├── relay_controller.h
    │   ├── switch_panel.h
    │   ├── viper_interface.h
    │   ├── ecu_node.h
    │   └── cardputer.h
    └── accessory_node/                         # single unified sketch (all nodes)
        ├── accessory_node.ino                  # setup() / loop() / frame dispatch
        ├── node_config.h                       # ← overwritten by Makefile before each build
        ├── node_state.h                        # extern g_relay_mirror, g_can_ok, g_menu_active
        ├── can_protocol.h                      # CAN message IDs, enums, CanRule struct + DSL macros
        ├── bus.h / bus.cpp                     # dual-transport abstraction (TWAI + ESP-NOW)
        ├── webui.h / webui.cpp                 # SoftAP + captive portal + HTTP + JSON API
        ├── index_html.h                        # embedded single-page web UI
        ├── mod_relay.h / mod_relay.cpp         # relay GPIO, watchdog, telemetry
        ├── mod_lcd.h / mod_lcd.cpp             # HD44780 16×2 driver; lcd_relay_char/lcd_relay_label helpers
        ├── mod_menu.h / mod_menu.cpp           # LCD menu system; requires ENABLE_MENU + MENU_HAS_* flags
        ├── mod_switches.h / mod_switches.cpp   # switch/button/encoder inputs → SWITCH_EVENT / ENCODER_EVENT only
        ├── mod_rules.h / mod_rules.cpp         # CAN-frame-triggered rules engine (replaces switch action dispatch)
        ├── mod_buzzer.h / mod_buzzer.cpp       # passive piezo tone sequencer; non-blocking via buzzer_tick()
        ├── mod_led.h / mod_led.cpp             # CAN-controllable status LEDs; responds to LED_CMD (0x102)
        ├── mod_error.h / mod_error.cpp         # unified error event system; emits/handles ERROR_EVENT (0x0F6)
        ├── mod_viper.h / mod_viper.cpp         # Viper 5305V serial bridge (ViperESP2 inlined)
        ├── mod_mpu6050.h / mod_mpu6050.cpp     # MPU-6050 accelerometer / shake detection
        ├── mod_dht22.h / mod_dht22.cpp         # AM2302 temperature/humidity sensor
        ├── mod_rpm.h / mod_rpm.cpp             # engine RPM via PC817C optocoupler + interrupt counting
        ├── mod_wbo2.h / mod_wbo2.cpp           # wideband O2 sensor analog read + WBO2_DATA broadcast
        ├── mod_ecu.h / mod_ecu.cpp             # dual-mode fuel controller (carb PI + TBI injection)
        ├── mod_gps.h / mod_gps.cpp             # GPS speed/heading via NMEA UART (u-blox Neo-6M/8M)
        ├── mod_mqtt.h / mod_mqtt.cpp           # MQTT bridge: publishes CAN frames, subscribes for injection (bridge only)
        ├── mod_blob.h / mod_blob.cpp           # generic chunked blob write protocol (BLOB_WRITE 0x410 + BLOB_COMMIT 0x411); 4-slot RX, up to 256 B per key
        ├── mod_wifi_creds.h / mod_wifi_creds.cpp # runtime WiFi + ESP-NOW credential storage (NVS "wifi_creds"); seeds from secrets.h on first boot
        ├── mod_battery.h / mod_battery.cpp     # dual-channel battery voltage ADC → CAN_ID_TELEMETRY (0x300)
        ├── mod_channels.h / mod_channels.cpp   # channel capability advertisement (CHAN_CAP_REQ / CHAN_CAP)
        ├── mod_m5_cardputer.h / mod_m5_cardputer.cpp # M5Stack Cardputer TFT + keyboard CAN terminal
        ├── secrets.h                           # ← gitignored; AP_SSID, AP_PASSWORD, ESPNOW_PMK, ESPNOW_LMK
        └── secrets.h.example                  # committed placeholder with zero ESP-NOW keys
```

**How it works:** `firmware/configs/<node>.h` defines which feature flags (`ENABLE_RELAY`, `ENABLE_LCD`, etc.) and pin assignments apply to that physical ESP32. The Makefile copies the right config to `node_config.h` before compiling, so the single sketch folder produces the correct firmware for each node. Every module's `.cpp` wraps its entire body in `#ifdef ENABLE_*` so unneeded modules compile to nothing.

**Adding a new node:** create `firmware/configs/<new_node>.h` with the desired `ENABLE_*` flags and pin assignments, then add a target in the Makefile that copies it and compiles.

## Architecture overview

Multiple ESP32 nodes (ESP32-WROOM-32 and ESP32-S3), each running up to four things concurrently:

1. **TWAI** (ESP32's CAN controller) — primary bus transport at 125 kbit/s.
2. **ESP-NOW** — secondary transport, broadcasts every frame to all peers on the same WiFi channel. Works as a failover if the wire breaks.
3. **SoftAP** — SSID configurable via `AP_SSID` in `secrets.h`, fixed channel 6, so phones roam to whichever node is closer and ESP-NOW peers find each other regardless of which AP the user is on.
4. **HTTP + DNS captive portal** — serves a terminal-style web UI at `http://192.168.4.1` with a CAN command line, live frame log, node status, and transport-mode selector (CAN+WiFi / WiFi-only / CAN-only).

`bus.cpp` is the transport abstraction. Application code never calls `twai_transmit` or `esp_now_send` directly — it calls `bus_tx()`, which fans out to both transports, and `bus_rx()`, which returns frames from either with (node_id, seq) dedup.

## Node identities

| Node             | NODE_ID | Config file                      | Features                                                                    |
|------------------|---------|----------------------------------|-----------------------------------------------------------------------------|
| switch_panel     | 0x01    | configs/switch_panel.h           | ENABLE_SWITCHES, ENABLE_RULES, ENABLE_LCD, ENABLE_MENU, ENABLE_BUZZER, ENABLE_LEDS, ENABLE_BATTERY, ENABLE_BLUETOOTH |
| relay_controller | 0x02    | configs/relay_controller.h       | ENABLE_RELAY, ENABLE_RULES, ENABLE_BATTERY, ENABLE_BLUETOOTH, ENABLE_RPM (GPIO 35), ENABLE_FUEL_PUMP_SAFETY (multi-gate; coil voltage ADC on GPIO 39 is internal to this module) |
| viper_interface  | 0x03    | configs/viper_interface.h        | ENABLE_VIPER, ENABLE_LCD, ENABLE_MPU6050                                    |
| ecu_node         | 0x04    | configs/ecu_node.h               | ENABLE_RPM, ENABLE_WBO2, ENABLE_ECU (MAP, TPS, CLT, IAT, carb solenoid, dual injectors) |
| bridge           | 0x05    | configs/bridge.h                 | BRIDGE_MODE — wired CAN + SoftAP + STA to home router; no ESP-NOW. Aggregated node discovery web UI. Optional MQTT publishing via mod_mqtt. |
| cardputer        | 0x06    | configs/cardputer.h              | ENABLE_M5_CARDPUTER + ENABLE_OTA_UPLOAD — M5Stack Cardputer (ESP32-S3); TFT CAN terminal + keyboard CLI; five screens: CAN (raw hex log), FEED (human-readable), COMMANDS (action macros), SETTINGS (BT/WiFi/speaker), OTA (SD-card firmware upload); ESPNOW_ONLY mode (no SoftAP/web server). |

## Feature flags (defined in configs/*.h)

| Flag              | Module              | Description                                                          |
|-------------------|---------------------|----------------------------------------------------------------------|
| `ENABLE_RELAY`    | mod_relay           | 6 relay GPIO outputs, safety watchdog                                |
| `ENABLE_SWITCHES` | mod_switches        | 10 inputs (6 latching + 4 buttons) + encoder; publishes SWITCH_EVENT / ENCODER_EVENT only |
| `ENABLE_RULES`    | mod_rules           | CAN-frame-triggered rules engine; `MAX_RULES` cap; `RULES_DEFAULT_INIT` for compile-time defaults |
| `ENABLE_LCD`      | mod_lcd             | HD44780 16×2 via PCF8574 I2C backpack; relay char/label helpers      |
| `ENABLE_MENU`     | mod_menu            | LCD menu system; requires ENABLE_LCD; per-section `MENU_HAS_*` flags |
| `ENABLE_BUZZER`   | mod_buzzer          | Passive piezo tone sequencer; `BUZZER_PIN` sets the GPIO             |
| `ENABLE_LEDS`     | mod_led             | CAN-controllable status LEDs; `NUM_LEDS`, `LED_PINS_INIT`, `LED_ACTIVE_HIGH` |
| `ENABLE_VIPER`    | mod_viper           | Viper 5305V serial bridge over UART2                                 |
| `ENABLE_MPU6050`  | mod_mpu6050         | MPU-6050 shake detection, IMU data broadcast                         |
| `ENABLE_DHT22`    | mod_dht22           | AM2302 temperature/humidity broadcast                                |
| `ENABLE_RPM`      | mod_rpm             | Engine RPM sensor (if `RPM_PIN` defined) and/or LCD bar widget (if `RPM_WIDGET_ROW` defined); redline configurable over CAN (`CFG_KEY_RPM_REDLINE 0x40`) |
| `ENABLE_FUEL_PUMP_SAFETY` | mod_fuel_pump | Multi-gate fuel pump FSM (PRIME → ARMED → RUNNING). Owns the relay at `FUEL_PUMP_RELAY` (default R1). Gates are RPM (bit 0) and COIL (bit 1); mode bitmask selects which gates are required. Default mode is `FUEL_PUMP_DEFAULT_MODE` (BOTH on the relay node = strictest). **Coil voltage ADC sampling is internal** to this module (samples `IGN_COIL_ADC_PIN` directly and applies hysteresis); the FSM no longer round-trips through `IGNITION_DATA` — the frame is still broadcast for observability but consumed internally. RPM gate consumes `CAN_ID_ENGINE_DATA (0x304)` from any node. On stall (`RUNNING → ARMED`), broadcasts `BUZZER_CMD` (`BUZZER_SEQ_FUEL_PUMP_OFF`) + `CAN_ID_FUEL_PUMP_STATE (0x311)` so the switch-panel buzzer and Cardputer both alert audibly. Params: `FUEL_PUMP_RELAY`, `FUEL_PUMP_PRIME_MS`, `FUEL_PUMP_RPM_THRESHOLD`, `FUEL_PUMP_STALL_MS`, `FUEL_PUMP_DEFAULT_MODE`, `IGN_COIL_ADC_PIN`, `IGN_COIL_DIVIDER_RATIO`, `IGN_COIL_ON_THRESHOLD_CV`, `IGN_COIL_OFF_THRESHOLD_CV`, `IGN_COIL_SAMPLE_MS`, `IGN_COIL_BROADCAST_MS`. Runtime mode change via `CFG_KEY_FUEL_PUMP_SAFETY (0x61)` or `RULE_ACT_FUEL_PUMP_SAFETY (17)` (arg0 = `FP_MODE_*` bitmask); state is RAM-only and resets to default on reboot. |
| `ENABLE_GPS`      | mod_gps             | GPS speed/heading via NMEA UART; `GPS_SERIAL_NUM`, `GPS_RX_PIN`, `GPS_TX_PIN`, `GPS_BAUD` |
| `ENABLE_WBO2`     | mod_wbo2            | Wideband O2 analog read (0–5V controller output); `WBO2_PIN`, `WBO2_SAMPLE_MS`, `WBO2_MIN/MAX_V`, `WBO2_MIN/MAX_AFR` |
| `ENABLE_ECU`      | mod_ecu             | Dual-mode fuel controller: Mode 0 = carb air-bleed solenoid (LEDC PWM PI loop); Mode 1 = TBI dual injectors (esp_timer pulse). Reads MAP, TPS, CLT, IAT. VE table + STFT closed-loop from WBO2_DATA. |
| `BRIDGE_MODE`     | (webui + mod_mqtt)  | Bridge node: SoftAP + wired CAN + WiFi STA to home router. No ESP-NOW. Web UI shows aggregated control panel built from NODE_CAP discovery. Enables `/api/nodecaps` endpoint. Requires `STA_SSID`/`STA_PASSWORD`. |
| `MQTT_BROKER`     | mod_mqtt            | Enable MQTT publishing on the bridge. Set to broker IP/hostname. Requires PubSubClient library. Publishes all CAN frames to `{MQTT_TOPIC_PREFIX}/frames`; subscribes to `{MQTT_TOPIC_PREFIX}/send` for injection. |
| `ENABLE_BATTERY`  | mod_battery         | Dual-channel battery voltage ADC; broadcasts CAN_ID_TELEMETRY (0x300). Requires `VBAT_ADC_PIN` and/or `VBAT2_ADC_PIN`. |
| `ENABLE_BLUETOOTH` | mod_bluetooth      | BLE GATT CAN bus mirror, **NimBLE-Arduino backend** (light-weight; ~30 KB RAM vs ~70 KB for Bluedroid, much better WiFi+BLE coexist — required to run alongside SoftAP/ESP-NOW/CAN on a WROOM). Notifies a connected phone of every frame (TX characteristic) and injects frames written by the phone (RX characteristic). NVS key `"bt_en"` in `NVS_NAMESPACE` enables/disables at boot. Controllable via `CFG_KEY_BT_ENABLED` (0x33) and `CFG_KEY_BT_ADVERTISING` (0x34) config writes; the switch panel menu broadcasts both keys with `CFG_TARGET_BROADCAST` so every BT-capable node flips together. Wire format: `[id_lo, id_hi, dlc, d0..d7]` (11 bytes, matches iOS Tuner `BLEManager.swift`). |
| `ENABLE_M5_CARDPUTER` | mod_m5_cardputer | M5Stack Cardputer TFT display + keyboard CLI; relay status bar (#006DAC). Screens: **CAN** (raw hex frame log), **FEED** (human-readable decoded frames), **COMMANDS** (action macros, internally `M5Screen::MENU`), **SETTINGS** (BT enable, WiFi enable, speaker volume), and **OTA** (SD-card firmware upload, gated by `ENABLE_OTA_UPLOAD`). `fn` toggles CAN↔COMMANDS (also returns to CAN from SETTINGS or OTA). `opt` enters status bar selection mode with the screen-label item selected first: `;`/`.` navigate relays and the screen label; Enter/Space toggles a relay or opens a dropdown (CAN / FEED / COMMANDS / SETTINGS [/ OTA]); Del or Opt exits. Status bar shows relay boxes (green=ON, grey=OFF; in selection mode selected=grey, unselected-OFF=acapulco-blue), battery gauge (dark green/yellow/red), peer count pill (green/red). CLI bar shows status events when CLI is closed. `;`/`.` scroll 8-entry command history in the CLI. Requires M5Cardputer library. |
| `ENABLE_OTA_UPLOAD` | mod_m5_cardputer | SD-card → HTTP POST OTA client on the Cardputer. Adds an **OTA** screen (5th item in the status-bar dropdown) that lists `.bin` files from `/canoe-firmwares/` on the inserted MicroSD, then uploads the selected file to `http://192.168.4.1/api/ota` after temporarily associating to the target AP (`WiFi.begin(AP_SSID, AP_PASSWORD)`). ESP-NOW stays on channel 6 throughout; only the STA association is added then dropped (`WiFi.disconnect(false, true)` + `esp_wifi_set_channel(6, ...)` restore). Requires `ENABLE_M5_CARDPUTER` and pulls in `SD.h` + `WiFi.h`. **Workflow today:** power off all other nodes before uploading so the SSID lookup finds the intended target — there is no per-node SSID disambiguation yet (see roadmap). |
| `ESPNOW_ONLY`    | (accessory_node.ino) | Skip SoftAP and web server; init ESP-NOW via `bus_init_no_ap()` only. Used by headless nodes like the Cardputer. |

`ENABLE_MENU` subflags (defined alongside `ENABLE_MENU` in the node config):

| Flag               | Submenu compiled in                        |
|--------------------|--------------------------------------------|
| `MENU_HAS_RELAYS`  | Relay toggle submenu (6 relays)            |
| `MENU_HAS_VIPER`   | Viper lock/unlock/start submenu            |
| `MENU_HAS_BUS`     | Live TWAI health counter display           |
| `MENU_HAS_DISPLAY` | LCD backlight toggle                       |
| `MENU_HAS_WIFI`    | Per-node WiFi enable/disable (sends CONFIG_WRITE; self-toggle restarts) |
| `MENU_HAS_REBOOT`  | Reboot any node by ID (Self / Relay Ctrl / Viper Ifc / ECU Node / All Nodes) via `REBOOT_CMD (0x0F5)` |

## Rules engine

`mod_rules` is the central action dispatcher. It replaces the old per-switch action map (`g_map[]` / `SwitchAction`). Every rule is a `CanRule` (12 bytes):

```c
struct CanRule {
  uint16_t trig_id;              // CAN ID to match
  uint8_t  c0_byte, c0_val, c0_mask;  // byte 0 condition (mask=0 → skip)
  uint8_t  c1_byte, c1_val, c1_mask;  // byte 1 condition
  uint8_t  action, arg0, arg1, arg2;  // what to do
};
```

Rules are stored in NVS (namespace `"rules"`, keys `"r0"` … `"rN"`). A compile-time `RULES_DEFAULT_INIT` macro in the node config provides the initial factory defaults.

**Trigger macros:**
```c
TRIG_SW_PRESS(idx)      // SWITCH_EVENT, switch idx, data[1] == SW_PRESS
TRIG_SW_RELEASE(idx)    // SWITCH_EVENT, switch idx, data[1] == SW_RELEASE
TRIG_SW_LONG(idx)       // SWITCH_EVENT, switch idx, data[1] == SW_LONG_PRESS
TRIG_RELAY_BIT_ON(n)    // RELAY_STATUS, bit n set
TRIG_RELAY_BIT_OFF(n)   // RELAY_STATUS, bit n clear
TRIG_RELAY_CMD_ON(n)    // RELAY_CMD sets bit n ON (change-driven)
TRIG_RELAY_CMD_OFF(n)   // RELAY_CMD sets bit n OFF
TRIG_BUS_ERROR()        // CAN_ID_BUS_ERROR (0x0F4) — any error code
TRIG_ERROR_EVENT()      // ERROR_EVENT (0x0F6) — any error (use c0/c1 to filter)
TRIG_ERROR_CRITICAL()   // ERROR_EVENT with severity >= CRITICAL
TRIG_ERROR_EMERGENCY()  // ERROR_EVENT with severity == EMERGENCY
TRIG_CAN_OK()           // CAN_ID_BUS_ERROR with error_code == 0 (recovery)
TRIG_BOOT()             // BOOT_EVENT (0x0F1) — fires once at end of setup()
```

**Action macros:**
```c
ACT_RELAY_TOGGLE(r)      ACT_RELAY_ON(r)       ACT_RELAY_OFF(r)
ACT_ALL_OFF()            ACT_LED_ON(node,led)  ACT_LED_OFF(node,led)
ACT_LED_FLASH(node,led,period_ds)              // flash LED; period_ds in 100 ms units
ACT_WIFI_ENABLE(node)    ACT_WIFI_DISABLE(node)
ACT_VIPER(cmd)           ACT_MENU_SELECT()     ACT_MENU_ENTER()
ACT_BUZZER_ALERT()       // 3 urgent 1047 Hz pulses; interrupts any active sequence
ACT_BUZZER_PLAY(target, seq)          // send BUZZER_CMD to target node; seq = BUZZER_SEQ_*
ACT_BUZZER_PLAY_ARG(target, seq, arg) // same with extra arg (e.g. peer count for BUZZER_SEQ_PEER)
ACT_RELAY_TIMED_OFF(r, secs)         // turn relay r on, then off after secs seconds
ACT_LED_FLASH(node, led, period_ds)  // flash LED; period_ds in 100 ms units
ACT_FUEL_PUMP_SAFETY(mode)           // set fuel pump mode bitmask (FP_MODE_*); relay-controller only
ACT_FUEL_PUMP_SAFETY_DISABLE         // mode 0: force pump on, freeze FSM (resets on reboot)
ACT_FUEL_PUMP_SAFETY_RPM_ONLY        // mode 1: RPM gate only
ACT_FUEL_PUMP_SAFETY_COIL_ONLY       // mode 2: COIL gate only
ACT_FUEL_PUMP_SAFETY_BOTH            // mode 3: RPM AND COIL (strictest)
ACT_FUEL_PUMP_SAFETY_ENABLE          // back-compat alias for RPM_ONLY
```

**Rule DSL:**
```c
#define RULES_DEFAULT_INIT \
  RULE(TRIG_SW_PRESS(0),  ACT_RELAY_TOGGLE(0)), \
  RULE(TRIG_SW_PRESS(1),  ACT_RELAY_TOGGLE(1)), \
  RULE(TRIG_SW_PRESS(4),  ACT_RELAY_ON(4)),     \
  RULE(TRIG_SW_RELEASE(4),ACT_RELAY_OFF(4)),    \
  RULE(TRIG_SW_PRESS(7),  ACT_ALL_OFF()),       \
```

HOLD-style behavior (relay on while switch pressed, off on release) is expressed with two rules (PRESS→ON, RELEASE→OFF). The relay controller's `RELAY_MAX_ON_MS` watchdog provides safety cutoff as a fallback.

**Self-echo:** `bus_tx()` feeds outbound frames back into the RX ring. Rules see their own emitted frames, and the buzzer, LCD, and relay mirror react to them exactly as they would to frames from other nodes.

**Runtime editing:** `/api/rules` REST endpoints (GET / POST / DELETE) and a **Rules** tab in the web UI allow viewing, creating, editing, and deleting rules at runtime. Changes are persisted to NVS via `rules_set()`. A factory reset endpoint (`POST /api/rules/reset`) restores `RULES_DEFAULT_INIT`. Each write/delete/reset also emits a `BLOB_WRITE` + `BLOB_COMMIT` frame pair (`BLOB_NS_RULES 0x02`) so the change is visible in the frame log; the commit callback on the same node applies the change locally (self-echoed commits are ignored by the blob handler, so the local apply is direct).

## Switch module

`mod_switches` is **input-only**. It polls GPIO state, debounces, and publishes:
- `SWITCH_EVENT (0x200)` — `[switch_id, event]` where event is 0 RELEASE, 1 PRESS, 2 LONG_PRESS, 3 DOUBLE_PRESS.
- `ENCODER_EVENT (0x201)` — `[event, count]` where event is 0 CW, 1 CCW, 2 PRESS, 3 RELEASE, 4 LONG_PRESS.

It has no action dispatch and no NVS config. All switch→action behavior lives in the rules engine. Encoder scroll-in-menu is handled directly in `accessory_node.ino`'s `bus_rx()` loop (encoder events are self-echoed so they appear in rx just like any other frame).

**ACK / retry:** Every `SWITCH_EVENT` is registered in a `PendingAck` slot. The SWITCH_ACK responder is gated behind `#ifdef ENABLE_RELAY` — only the relay controller sends `CAN_ID_SWITCH_ACK (0x202) [switch_id, event]`. Observer nodes (Cardputer, bridge) no longer falsely ACK. If no ACK arrives within 80 ms the frame is retransmitted up to 3 times. After exhausting retries: `buzzer_alert()` fires + a `LED_CMD` flashes LED index 2 (the error indicator).

## Pinout

All nodes share the same CAN pins. Node-specific pins are defined in the config header.

| Pin       | Role                                                           |
|-----------|----------------------------------------------------------------|
| GPIO 5    | CAN TX (all nodes — to transceiver TXD, or shared bus in bench mode) |
| GPIO 4    | CAN RX (all nodes — from transceiver RXD, or shared bus)      |
| GPIO 16–22| Relay controller: ULN2803 inputs (relays 1–6)                  |
| GPIO 25,26,27,32,33,13 | Switch panel: switches to GND                    |
| GPIO 34, 35 | Switch panel: rotary encoder GA (CLK/A) and GB (DT/B). Module is CJMCU-111 (EC11-based). Has onboard 3.3 kΩ pull-ups (marked 332) — connect module VCC to 3V3, no external resistors needed. The shaft physically clicks but the SW contact is not wired to any pin header on the CJMCU-111 PCB — use a dedicated panel button (BTN1–4) for encoder select/back instead. |
| GPIO 16   | Switch panel: passive piezo buzzer (`BUZZER_PIN`). Positive leg to GPIO 16 via optional 100Ω series resistor; negative leg to GND. Driven by `tone()`/`noTone()`. |
| GPIO 17, 19, 23 | Switch panel: status LEDs (`LED_PINS_INIT`). Each drives an LED via a 330Ω series resistor to GND (`LED_ACTIVE_HIGH true`). Controlled via CAN_ID_LED_CMD. |
| GPIO 36, 39 | Switch panel: BTN3 and BTN4 (moved from GPIO 19/23 to free those for LED outputs). Input-only pins — **no internal pull-up**; wire a 10kΩ resistor from each pin to 3V3. |
| GPIO 34   | Relay controller: primary battery voltage ADC (input-only)     |
| GPIO 36   | Relay controller: auxiliary battery voltage ADC (input-only)   |
| GPIO 16   | Viper interface: UART2 TX → level shifter → Viper serial RX    |
| GPIO 17   | Viper interface: UART2 RX ← level shifter ← Viper serial TX   |

GPIO 13 is a strapping pin on some ESP32 boards. If SW6 acts weird at boot, move it to another GPIO.

Note: GPIO 16/17 are relay outputs on the relay_controller board and UART2 on the viper_interface board — these are different physical ESP32s, so there is no conflict.

## CAN protocol (11-bit IDs)

| ID     | Name              | Payload                                                |
|--------|-------------------|--------------------------------------------------------|
| 0x0F0  | NODE_ANNOUNCE     | `[node_id, peer_count, can_ok]` — every node broadcasts every 5 s; node_id in data[0] identifies sender |
| 0x0F1  | BOOT_EVENT        | `[node_id]` — emitted once at end of setup(), self-echoed; used by TRIG_BOOT() in rules |
| 0x0F2  | NODE_CAP          | `[node_id, caps, switch_count, button_count, led_count, relay_count]` — capability advertisement; broadcast at boot and every 30 s; also sent in response to NODE_CAP_REQ. caps bits: 0x01=relay, 0x02=switches, 0x04=viper, 0x08=leds, 0x10=rules |
| 0x0F3  | NODE_CAP_REQ      | `[target_node_id]` — request capability frame; 0xFF = all nodes respond |
| 0x0F4  | BUS_ERROR         | `[node_id, error_code, tx_err_cnt, rx_err_cnt]` — emitted on error transitions (BUS_OFF=1, ERROR_PASSIVE=2, TX_FAIL=3, RX_OVERFLOW=4); error_code=0 signals recovery. Sent over both transports so ESP-NOW carries it even when wired CAN has failed. |
| 0x0F5  | REBOOT_CMD        | `[target_node_id]` — any node may send; target calls `ESP.restart()` when its node_id matches or target is 0xFF (all nodes). Menu shows LCD "Rebooting..." for local reboots. |
| 0x0F6  | ERROR_EVENT       | `[source_node, error_code, severity, target_node, flags, arg0, arg1, arg2]` — unified error/alert frame. severity: 0=INFO, 1=WARNING, 2=CRITICAL, 3=EMERGENCY. target 0xFF=all nodes. flags: bit0=active, bit1=audible, bit2=visual, bit3=latching. Error codes grouped by subsystem (0x01–0x1F bus, 0x20–0x3F power, 0x40–0x5F engine, 0x60–0x7F sensors, 0x80–0x9F comms, 0xA0+ app). Receivers map severity to hardware: buzzer plays escalating tones, LEDs flash, Cardputer shows event + beep. Latching errors persist until an explicit clear (flags bit0=0) is received. Non-latching errors auto-clear after 10 s. |
| 0x100  | RELAY_CMD         | `[mask, state]` — only bits set in mask are applied    |
| 0x101  | RELAY_STATUS      | `[bitmap]` — broadcast at 5 Hz                         |
| 0x102  | LED_CMD           | 3-byte form: `[target_node_id, mask, state]`; 4-byte form: `[target_node_id, mask, state, flash_period_ds]` — flash_period_ds in 100 ms units (0 = solid). Any node → target; 0xFF target = broadcast. |
| 0x103  | LED_STATUS        | `[node_id, bitmap]` — sent by target on change         |
| 0x104  | BUZZER_CMD        | `[target_node_id, cmd, arg0]` — target 0xFF = broadcast. cmd = `BUZZER_SEQ_*` (0x01–0x13) or `BUZZER_CMD_MUTE (0x20)`. arg0 is optional (peer count for `BUZZER_SEQ_PEER`, mute flag for `BUZZER_CMD_MUTE`). `BUZZER_SEQ_FUEL_PUMP_OFF (0x13)` is a 4-pulse low/high alarm used when the fuel pump safety FSM cuts power on stall. |
| 0x200  | SWITCH_EVENT      | `[switch_id, SwitchEvent]`                             |
| 0x201  | ENCODER_EVENT     | `[EncoderEvent, count]`                                |
| 0x202  | SWITCH_ACK        | `[switch_id, event]` — relay controller (`ENABLE_RELAY` nodes only) ACKs a SWITCH_EVENT; clears the switch panel's retry timer for that event |
| 0x300  | TELEMETRY         | `[vbat_cv_lo, vbat_cv_hi, 0, 0, vbat2_cv_lo, vbat2_cv_hi, 0, 0]` — from mod_battery; primary battery centvolts in bytes 0–1, auxiliary in bytes 4–5 |
| 0x301  | ENV_DATA          | `[temp_d1_lo, temp_d1_hi, humi_d1_lo, humi_d1_hi]` (0.1°C, 0.1%) |
| 0x302  | IMU_DATA          | `[accel_x_lo, accel_x_hi, accel_y_lo, accel_y_hi, accel_z_lo, accel_z_hi]` |
| 0x303  | SHAKE_EVENT       | `[magnitude, axis_mask]`                               |
| 0x304  | ENGINE_DATA       | `[rpm_lo, rpm_hi]` — uint16 LE RPM; broadcast at `RPM_SAMPLE_MS` interval |
| 0x305  | GPS_DATA          | `[speed_lo, speed_hi, heading_lo, heading_hi, flags]` — speed 0.1 mph, heading 0.1 deg, flags: bit0=fix, bit1=speed valid, bit2=heading valid |
| 0x306  | WBO2_DATA         | `[afr_lo, afr_hi]` — uint16 LE, AFR × 100 (e.g. 1470 = 14.70 AFR)                                                                             |
| 0x307  | ECU_DATA          | `[mode, map_kpa, tps_pct, clt_enc, iat_enc, pw_lo, pw_hi, flags]` — mode: 0=carb 1=inject; temps encoded as °C+40; pw = duty×100 (carb) or μs (inject); flags: bit0=closed_loop, bit1=enriching, bit2=inj_saturated, bit3=running |
| 0x308  | ECU_CMD           | `[cmd, arg0, arg1, arg2]` — cmd: 0x01 set mode (arg0: 0/1), 0x02 set target AFR×100 (arg1+arg2 uint16 LE), 0x03 fuel cut (arg0: 0/1), 0x04 reset trim |
| 0x310  | IGNITION_DATA     | `[coil_cv_lo, coil_cv_hi, ign_on]` — coil + voltage in centivolts (int16 LE) and a 1/0 byte derived from on/off hysteresis thresholds. Broadcast every ~1 s as a heartbeat and immediately on every on/off edge. |
| 0x311  | FUEL_PUMP_STATE   | `[state, mode, reason, gates_ok]` — state: 0=PRIME 1=ARMED 2=RUNNING. mode: FuelPumpMode bitmask (0=off, 1=RPM, 2=COIL, 3=BOTH). reason: 0=none 1=boot 2=prime_done 3=gate_pass 4=stall 5=mode_change 6=re_enable. gates_ok: bit 0 = RPM gate currently passing, bit 1 = COIL gate currently passing. Emitted on every state transition. |
| 0x400  | CONFIG_WRITE      | `[target, key, index, _reserved, arg, arg2_lo, arg2_hi, flags]` |
| 0x401  | CONFIG_READ_REQ   | `[target, key, index]` (index 0xFF = all)              |
| 0x402  | CONFIG_READ_RESP  | same layout as CONFIG_WRITE (flags byte unused)        |
| 0x403  | CONFIG_SAVE       | `[target, action]` — 0x01 commit, 0x02 reload, 0x03 factory reset |
| 0x500  | LCD_CMD           | `[row, col, char…]` or `[0xFF]` clear — any node → switch_panel |
| 0x410  | BLOB_WRITE        | `[target, ns, key, chunk_idx, d0, d1, d2, d3]` — 4-byte chunk of a multi-byte value. chunk_idx × 4 = byte offset. target 0xFF = broadcast. |
| 0x411  | BLOB_COMMIT       | `[target, ns, key, len_lo, len_hi, flags]` — finalizes a blob transfer; fires commit callback on receiver. flags: `BLOB_FLAG_PERSIST=0x01` (save to NVS), `BLOB_FLAG_REBOOT=0x02` (restart after saving). Self-echoed frames are ignored by the blob handler; the sender saves its own copy directly. |
| 0x510  | VIPER_CMD         | `[cmd]` — any node → viper_interface; cmd: 0x01 lock, 0x02 unlock, 0x03 remote start |
| 0x511  | VIPER_STATUS      | `[b0..b4]` — viper_interface → everyone; raw 5-byte Viper alarm packet |
| 0x320  | CHAN_CAP_REQ      | `[target_node_id]` — request channel capability advertisement; 0xFF = all nodes respond |
| 0x321  | CHAN_CAP          | `[node_id, chan_id, can_id_lo, can_id_hi, offset, encoding, min, max]` — one frame per advertised data channel; encoding flags define type/scale |

Config targets: `0x01 SWITCH_PANEL`, `0x02 RELAY_CTRL`, `0x03 VIPER`, `0x04 ECU`, `0x05 BRIDGE`, `0x06 CARDPUTER`, `0xFF BROADCAST`.

Config keys:
- `0x01 CFG_KEY_NODE_ID` — reassign node_id; arg (data[4]) = new ID (0x01–0xFE); saves to NVS and restarts. Broadcast target **not accepted**.
- `0x20 CFG_KEY_RELAY_MAX_ON_MS` — per-relay safety auto-off timeout (arg2_lo/hi = ms, 0 = no limit; index = relay 0–5)
- `0x30 CFG_KEY_WIFI_ENABLED` — legacy: sets both ap_en and espnow_en; arg=0/1; node restarts
- `0x31 CFG_KEY_AP_ENABLED` — enable/disable SoftAP + web server; arg=0/1; node restarts
- `0x32 CFG_KEY_ESPNOW_ENABLED` — enable/disable ESP-NOW radio; arg=0/1; node restarts
- `0x33 CFG_KEY_BT_ENABLED` — enable/disable BLE; arg=0/1; saves `bt_en` to NVS, node restarts (no-op without `ENABLE_BLUETOOTH`)
- `0x34 CFG_KEY_BT_ADVERTISING` — start/stop BLE advertising; arg=0/1; runtime only, no restart
- `0x40 CFG_KEY_RPM_REDLINE` — RPM redline for display widget (arg2_lo/hi = uint16 RPM)
- `0x51 CFG_KEY_ECU_MODE` — 0=carb, 1=inject; arg[4]=value; persisted to NVS
- `0x52 CFG_KEY_ECU_TARGET_AFR` — target AFR × 100 (uint16 LE in arg2_lo/hi)
- `0x53 CFG_KEY_ECU_BASE_PW` — injection base pulse width μs at 100% VE, 100 kPa (uint16 LE)
- `0x61 CFG_KEY_FUEL_PUMP_SAFETY` — relay-controller only; data[4] = `FuelPumpMode` bitmask: 0=OFF (pump forced on), 1=RPM, 2=COIL, 3=BOTH (default). RAM-only, resets to `FUEL_PUMP_DEFAULT_MODE` on reboot. Identical semantics to `ACT_FUEL_PUMP_SAFETY_*` — both call `fuel_pump_set_mode()`.

Blob namespaces (`BLOB_NS_*`):
- `0x01 BLOB_NS_WIFI` — WiFi / ESP-NOW credential transfer. Keys: `0x01 BLOB_KEY_SSID` (string), `0x02 BLOB_KEY_PASS` (string), `0x03 BLOB_KEY_PMK` (16 bytes), `0x04 BLOB_KEY_LMK` (16 bytes). Handled by `mod_wifi_creds`; persisted to NVS namespace `"wifi_creds"`. First boot seeds from `secrets.h`; runtime updates arrive via blob transfer or via `/api/wifi_creds` POST. Broadcasting with `BLOB_FLAG_PERSIST` (no `BLOB_FLAG_REBOOT`) then sending `REBOOT_CMD 0xFF` ensures all nodes save before simultaneously restarting with new credentials. **`wifi_creds_on_blob()` validates every key before applying** — SSID 1..32 chars, PASS empty or 8..63 chars, PMK/LMK exactly 16 bytes. Out-of-range values are rejected with a `[wcreds] reject ...` log line and the existing NVS entry is preserved. This guards against partial blob delivery (lost chunks on CAN/ESP-NOW) silently saving a < 8 char password that `softAP()` would then downgrade to an open AP, leaving clients in a WPA handshake-fail loop.
- `0x02 BLOB_NS_RULES` — Rules engine NVS update. Key = rule slot index (0–MAX_RULES-1); value = 12-byte `CanRule` blob. Sentinel key `0xFE` triggers factory reset. Emitted by `/api/rules` POST/DELETE/reset so rule changes appear in the CAN frame log and the commit callback applies them locally. `BLOB_FLAG_PERSIST` is always set.

Switch events (0x200 data[1]): `0 RELEASE`, `1 PRESS`, `2 LONG_PRESS`, `3 DOUBLE_PRESS`.

Encoder events (0x201 data[0]): `0 ENC_ROTATE_CW`, `1 ENC_ROTATE_CCW`, `2 ENC_PRESS`, `3 ENC_RELEASE`, `4 ENC_LONG_PRESS`. data[1] = step count for rotation events.

## Bench vs transceiver mode

In `firmware/configs/<node>.h`:

```cpp
#define USE_CAN_TRANSCEIVER 0  // 0 = bench, 1 = production with transceiver
```

All nodes MUST agree. In bench mode, TWAI runs in `TWAI_MODE_NO_ACK` and the TX pin is set to open-drain via the GPIO pad register (`GPIO.pin[5].pad_driver = 1`). A single external pull-up (1k–4.7k to 3V3) on the shared wire emulates CAN's wired-AND behavior. Stay at 125 kbit/s and keep runs under ~1 m.

**Important:** Do **not** use `gpio_set_direction()` to make the TX pin open-drain — it disconnects the TWAI peripheral's signal routing through the GPIO matrix. The `pad_driver` register bit changes only the output driver mode without breaking the peripheral binding.

In production mode, connect TJA1051T/3 or SN65HVD230 transceivers with 120Ω termination at each physical end of the bus — no other firmware change needed. Production mode uses `TWAI_MODE_NORMAL` (ACK required).

## Libraries and toolchain

Everything is in the Arduino-ESP32 core except the bridge node's MQTT module and the Cardputer display/keyboard library.

- `driver/twai.h` — CAN controller
- `WiFi.h` / `esp_now.h` — wireless
- `WebServer.h` / `DNSServer.h` — HTTP + captive portal
- `Preferences.h` — NVS persistence
- `PubSubClient` — MQTT client (bridge only, when `MQTT_BROKER` is defined); install with `arduino-cli lib install "PubSubClient"`
- `NimBLE-Arduino` — BLE stack used by `mod_bluetooth` (any node with `ENABLE_BLUETOOTH`). Install with `arduino-cli lib install "NimBLE-Arduino"`. Do NOT install or use the Arduino-ESP32 `BLEDevice` library — it is the heavy Bluedroid stack that broke WiFi coexist before; the Makefile's BLE-capable build paths assume NimBLE.
- `M5Cardputer` — M5Stack Cardputer display + keyboard (cardputer node only); requires M5Stack board package URL in arduino-cli config

**Board packages:**
- `esp32:esp32` — Espressif ESP32 core (all nodes except Cardputer)
- `m5stack:esp32` — M5Stack ESP32 core (Cardputer only); add board manager URL: `https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json`

Tested against Arduino-ESP32 **v2.x and v3.x**. The ESP-NOW receive callback signature changed in v3.x (`esp_now_recv_info_t*` first arg instead of `const uint8_t* mac`); `bus.cpp` uses a version preprocessor guard to handle both. TWAI APIs also changed in v3.x — if bumping, verify compilation before committing.

## Build & flash (Mac)

Using arduino-cli:

```bash
make relay                                         # compile for relay controller
make switch                                        # compile for switch panel
make viper                                         # compile for viper interface
make ecu                                           # compile for ECU node
make bridge                                        # compile for bridge node
make cardputer                                     # compile for M5 Cardputer (ESP32-S3)
make all                                           # compile all five in sequence (excludes cardputer)
make upload-switch  PORT=/dev/cu.usbserial-XXXX
make upload-relay   PORT=/dev/cu.usbserial-YYYY
make upload-viper   PORT=/dev/cu.usbserial-ZZZZ
make upload-ecu     PORT=/dev/cu.usbserial-WWWW
make upload-bridge  PORT=/dev/cu.usbserial-VVVV
make upload-cardputer                              # copies bin to /Volumes/CARDPUTER/CANoE/ then unmounts
make monitor        PORT=/dev/cu.usbserial-XXXX

# OTA upload over WiFi (connect to node AP first; OTA_IP defaults to 192.168.4.1).
# A link-quality preflight pings OTA_IP and aborts on any packet loss or RTT
# above OTA_MAX_RTT_MS (default 30 ms). Override with OTA_FORCE=1.
make ota-switch                                    # OTA flash switch_panel; preflight runs first
make ota-relay   OTA_IP=192.168.4.1                # explicit IP if the node is on STA (bridge)
make ota-bridge  OTA_MAX_RTT_MS=60                 # loosen the gate
make ota-ecu     OTA_FORCE=1                       # skip preflight entirely (not recommended)

# Wipe entire flash (bootloader + NVS + app + otadata + spiffs). Required when
# recovering from corrupted WiFi creds / stuck node IDs / saved-rules issues —
# a plain `make upload-*` only rewrites the app partition and leaves NVS intact.
# Override esptool path with ESPTOOL=/path/to/esptool if Arduino15 lives elsewhere.
make erase-flash    PORT=/dev/cu.usbserial-XXXX    # then re-run make upload-<node>
```

Each `make <node>` target copies `firmware/configs/<node>.h` → `firmware/accessory_node/node_config.h` then compiles. `node_config.h` is intentionally not committed with a real config — the placeholder will error if you try to compile without running make first.

Or compile directly without Make (after manually copying the config):

```bash
cp firmware/configs/relay_controller.h firmware/accessory_node/node_config.h
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/accessory_node
arduino-cli upload -p /dev/cu.usbserial-XXXX --fqbn esp32:esp32:esp32 firmware/accessory_node
```

List serial ports: `ls /dev/cu.usbserial-* /dev/cu.wchusbserial-* /dev/cu.SLAB_USBtoUART 2>/dev/null`

Serial monitor at 115200 baud on each node for boot logs. Nodes with `USE_WIFI 1` also expose serial output in the web UI's **Serial** tab (via `wlog()`/`wlogln()` — replacements for `Serial.printf`/`Serial.println` that tee to both UART and the web UI).

## Web console cheat sheet

Connect phone/laptop to SSID **AccessoryBus** (open by default — add a password in `webui.cpp` before field use). Captive portal usually auto-pops; otherwise `http://192.168.4.1`.

The UI has four tabs:
- **CAN Frames** — raw frame log + hex command line + quick-action buttons.
- **Serial** — live tee of `wlog()`/`wlogln()` output (same as the UART monitor).
- **Control** — node-adaptive panel. Switch panel shows: physical switch state (tracked from SWITCH_EVENT frames; clickable to simulate input), momentary buttons (clickable), LED status dots (clickable to toggle). Relay controller shows: 6 relay tiles + All OFF. Viper interface shows Lock/Unlock/Start and last alarm response. Relay state updates in real time from RELAY_STATUS and RELAY_CMD frames. **Bridge node** shows an aggregated panel with one section per discovered node, built from NODE_CAP frames received over CAN; includes a Refresh button and a "Request All Caps" button for nodes that haven't announced yet.
- **Rules** — full rules editor; lists all rules, shows human-readable trigger/action descriptions, add/edit/delete rules inline, factory reset button. Hidden on the bridge node (rules live on each node locally).
- **Network** — live node presence map, transport health (CAN/WiFi), last-seen times.
- **Settings** (hamburger menu) — node ID reassignment (saves to NVS, restarts) + **WiFi Credentials** panel (SSID, password, PMK hex, LMK hex; Load Current / Save to This Node / Broadcast to All Nodes / Reboot All buttons). Broadcast uses blob transfer with `BLOB_FLAG_PERSIST`; Reboot All sends `REBOOT_CMD 0xFF`. Changing SSID/password will disconnect your current browser session.
- **Serial** (hamburger menu) — live tee of `wlog()`/`wlogln()` UART output.

### Raw hex command line
```
<id_hex> <byte0> <byte1> ...       # up to 8 bytes
```

Examples:
```
100 01 01        # relay 1 on
100 3F 00        # all relays off
510 01           # viper lock/arm
510 02           # viper unlock/disarm
510 03           # viper remote start
```

### Aliases (shortcut commands, start with `:`)
```
:relay <n> on|off              # n = 1..6
:alloff                        # all 6 off
:horn                          # turn relay 5 on (subject to 30 s watchdog)
:readcfg relay                 # dump relay max-on-ms config
:save relay                    # commit relay config to NVS
:reset relay                   # factory reset relay config
:cfgrelay <idx> maxon <ms> [!] # set per-relay safety auto-off
:viper lock|unlock|start       # send VIPER_CMD to viper_interface node
```

Trailing `!` persists the change to NVS immediately.

### Wire-failure fallback test
Use the transport-mode selector in the web UI header: **CAN+WiFi** (normal), **WiFi only** (simulates wire failure), **CAN only** (disables ESP-NOW). `bus_tx()` respects the current `BusTxMode`; ESP-NOW framing still applies when WiFi is active.

## Conventions and gotchas

- **Init order.** In `setup()` the order must be `relay_setup()` → `setup_can()` → `webui_init()` → `bus_init()`. `webui_init` brings up WiFi so `esp_now_init` has a radio to bind to; `bus_init` requires WiFi ready. Relay pins are initialized first so outputs are known-good before any CAN traffic.
- **Frame-flow direction.** Application code only goes through the bus. Do not call `twai_transmit` directly except inside `bus.cpp`; it will bypass ESP-NOW and the web UI log.
- **Self-echo.** `bus_tx()` feeds every outbound frame back into the RX ring (tagged `source="self"`). This means LCD updates, rules, and the relay controller's own state all react to web-UI-injected frames exactly the same as frames from other nodes. Don't be surprised when you see your own TX come back through `bus_rx()`.
- **Relay mirror.** `g_relay_mirror` on non-relay nodes is updated only from `RELAY_STATUS` frames (confirmed state), not optimistically from `RELAY_CMD`. The relay controller itself still updates immediately from CMD since it is the executor. On non-relay nodes with `ENABLE_BUZZER`, any outbound `RELAY_CMD` starts a 500 ms confirmation timer; if no matching `RELAY_STATUS` arrives before the deadline, `buzzer_alert()` fires (same error tone as the SWITCH_EVENT ACK timeout). This catches relay-controller-offline scenarios.
- **Dedup.** ESP-NOW frames carry their own `(node_id, seq)` header; a 2-second window filters repeats. CAN frames are canonical and always passed through. This is safe because our application messages are idempotent (RELAY_CMD, STATUS, TELEMETRY) or edge-triggered with short windows (SWITCH_EVENT).
- **Captive-portal probes.** iOS and Android each hit different URLs to detect captive portals — we catch the common ones in `webui.cpp` and bounce them to `/`.
- **Credentials live in `secrets.h` (compile-time) and NVS (runtime).** `firmware/accessory_node/secrets.h` is gitignored and holds `AP_SSID`, `AP_PASSWORD`, `ESPNOW_PMK` (16 bytes), `ESPNOW_LMK` (16 bytes). Copy `secrets.h.example` and fill in real values before building. At first boot (or after `wifi_creds_reset()`), `mod_wifi_creds` seeds NVS `"wifi_creds"` from `secrets.h`. Subsequent boots load from NVS. The Settings panel's WiFi Credentials section or a blob transfer can update credentials at runtime without reflashing.
- **`make upload-*` does NOT erase NVS.** `arduino-cli upload` only rewrites the app (`app0`) partition; the `nvs`, `otadata`, `app1`, and `spiffs` partitions are untouched. Stale or corrupted WiFi creds, reassigned node IDs, and saved rules therefore survive a reflash and `wifi_creds_setup()` will keep using them. To force a re-seed from `secrets.h`, run `make erase-flash PORT=...` first, then `make upload-<node>`. Same applies for any other NVS-backed state (rules engine, ECU config, BT enable flag).
- **WiFi creds blob receive is validated.** `wifi_creds_on_blob()` enforces SSID 1..32 chars, PASS empty or 8..63 chars, PMK/LMK exactly 16 bytes; out-of-range values are rejected (logged as `[wcreds] reject ...`) and the existing NVS entry is preserved. This catches partially-delivered broadcasts that would otherwise save a < 8 char password — which `WiFi.softAP()` silently downgrades to an open AP, leaving WPA2 clients in a permanent handshake-fail loop. If you hit "AP up, clients flap 0↔1, macOS re-prompts for password" symptoms after a credential broadcast, the receiver's NVS is the prime suspect — wipe with `make erase-flash`.
- **OTA over WiFi is link-quality gated.** Every `make ota-*` target depends on `ota-check`, which pings `OTA_IP` 5× and aborts on any packet loss or avg RTT above `OTA_MAX_RTT_MS` (default 30 ms). A flaky OTA can stall the WiFi task long enough to wedge the AP and the host's WiFi supplicant. Tune with `OTA_MAX_RTT_MS=NN`; bypass entirely with `OTA_FORCE=1`. The OTA writes only the inactive app partition (otadata flips on `Update.end(true)`), so a failed OTA cannot brick a node by itself — but the link instability around it can require erase-flash recovery downstream.
- **ESP-NOW encryption.** `bus_init()` calls `esp_now_set_pmk()` with the key from `mod_wifi_creds` (falling back to `secrets.h` if NVS is unpopulated). The broadcast peer (FF:FF:FF:FF:FF:FF) cannot be encrypted by hardware, so initial peer discovery still uses broadcast. On first receipt of a frame from any MAC, `enc_peer_add()` registers that MAC as an encrypted unicast peer using the LMK from `mod_wifi_creds`. Subsequent `bus_tx()` calls send to all encrypted peers *and* broadcast; `(node_id, seq)` dedup handles the overlap.
- **Dynamic ESP-NOW keys.** Call `bus_set_espnow_keys(pmk, lmk)` before `bus_init()` to inject runtime keys. Both `esp_now_set_pmk()` and `enc_peer_add()` check an internal `g_espnow_keys_set` flag and fall back to the compile-time `secrets.h` values if not set. `accessory_node.ino` calls this from `wifi_creds_get_pmk/lmk()` in setup before any bus init path.
- **SoftAP client notifications.** `webui_set_ap_client_cb()` registers a callback fired when `WiFi.softAPgetStationNum()` changes (polled every 500 ms in `webui_tick()`). `accessory_node.ino` uses this to call `lcd_set_event()` and `buzzer_wifi_connect/disconnect()` when a device joins or leaves the AP.
- **Open AP.** Fine in a garage, risky in public. `AP_PASSWORD` in `secrets.h` must be ≥ 8 chars for WPA2 and identical on every WiFi node. `AP_SSID` and `AP_HIDDEN` are configurable there too.
- **Horn safety.** `RELAY_MAX_ON_INIT` in `configs/relay_controller.h` caps relay 5 (horn) at 30s. HOLD-style rules (SW_PRESS→relay ON, SW_RELEASE→relay OFF) still rely on the relay controller's watchdog as a backstop.
- **ADC calibration.** Battery voltage monitoring is handled by `mod_battery` (enabled via `ENABLE_BATTERY`). GPIO pins must be ADC1 (GPIO 32–39) since ADC2 conflicts with WiFi. `VBAT_DIVIDER_RATIO` and `VBAT2_DIVIDER_RATIO` in the node config assume a 10k + 2.2k divider. Re-tune to your actual resistors before trusting the telemetry.
- **Strapping pins.** Avoid GPIO 0, 2, 12, 15 for anything that's externally driven at reset. GPIO 13 is borderline — watch for flakiness.
- **ULN2803 polarity.** Input high → output low → coil pulled to GND → relay on. `RELAY_ACTIVE_HIGH = true` is correct for ULN2803 + low-side-coil relays. Flip if using high-side-switch drivers.
- **node_config.h is generated.** Never edit `firmware/accessory_node/node_config.h` directly — it gets overwritten by the next `make` invocation. Edit the appropriate `firmware/configs/<node>.h` instead.
- **I2C initialized once.** `Wire.begin()` is called a single time in `accessory_node.ino` setup before any module setup. Calling it inside a module (lcd_setup, mpu_setup, etc.) would reinitialize the ESP32 I2C peripheral and corrupt the LCD 4-bit mode state. Do not add `Wire.begin()` calls inside modules.
- **CGRAM slot 0 is forbidden.** HD44780 custom characters use CGRAM slots 0–7, but slot 0 maps to `\x00` (C null terminator) which silently truncates any `snprintf` output that contains it. Icon allocation in `lcd_setup()` starts at slot 1; max 7 custom icons total across all relays (2 of those slots are consumed by the shared CAN/WiFi status icons). Macros: `RELAY_n_LABEL`, `RELAY_n_ICON_ON`, `RELAY_n_ICON_OFF` in the node config define per-relay LCD labels and 5×8 CGRAM bitmaps.
- **LCD widget system.** Modules call `lcd_register_widget()` during setup to claim a screen region (row, col, width, refresh_ms, render fn). `lcd_update_status()` renders all widgets after the status row. `lcd_tick()` (called from loop) re-renders widgets on their own schedule. `lcd_set_event()` automatically truncates event text to the first widget boundary on row 1 so both coexist. `LcdWidget` struct is defined in `mod_lcd.h` and available regardless of `ENABLE_LCD` (stubs compile cleanly).
- **LCD status row format.** Row 0 idle display is `C[icon]W[icon] [relay chars]` where `C` = CAN, `W` = WiFi, icons are filled/hollow CGRAM glyphs (or `+`/`-` ASCII fallback). The shared on/off icon bitmaps are loaded at the end of `lcd_setup()` after relay icons.
- **`/api/config` endpoint.** `webui.cpp` includes `node_config.h` and serves a JSON config object with `has_relay`, `has_switches`, `has_viper`, `has_rules`, `has_leds`, `is_bridge`, `switch_count`, `button_count`, `led_count`, and `relay_labels[6]`. The Control tab fetches this on load to decide which sections to render.
- **`/api/nodecaps` endpoint.** All WiFi nodes serve a JSON array of the NODE_CAP frames they've received, including `id`, `caps`, `switch_count`, `button_count`, `led_count`, `relay_count`, and `age_ms`. The bridge web UI fetches this to build its aggregated control panel.
- **Node capability broadcast.** Every node calls `send_node_cap()` at boot and every 30 s (piggybacked on the 5 s announce, firing every 6th cycle). Any node can also request caps with a `NODE_CAP_REQ (0x0F3)` frame; all nodes (or a specific target) respond immediately. `BusFrame.source` is `"self"` for self-echoed frames, `"can"` for wired CAN, `"wifi"` for ESP-NOW — use this to distinguish sources since `BusFrame` has no `outbound` field.
- **GPIO 36/39 have no internal pull-up.** These are RTC-domain input-only pins on the ESP32. `INPUT_PULLUP` is silently ignored; the pull-up request has no effect. Any input on these pins needs an external 10kΩ resistor to 3V3. BTN3 and BTN4 were moved here from GPIO 19/23 to free those pins for LED outputs — document the external resistor requirement clearly for anyone who assembles the hardware.
- **Buzzer sequencer.** `mod_buzzer` uses Arduino `tone()`/`noTone()` to drive a passive piezo — no LEDC setup required. Sequences are arrays of `{freq, ms}` notes advanced by `buzzer_tick()` each loop; a new `play_seq()` call immediately interrupts any active sequence. Relay sounds fire on `RELAY_STATUS` frames (confirmed state changes), not `RELAY_CMD` — so no misleading tone plays when the relay controller is offline. `BUZZER_PIN` must not be an input-only GPIO (avoid 34/35).
- **`/api/wifi_creds` endpoint.** GET returns `{ssid, pass, pmk_hex, lmk_hex}`. POST body: JSON with any subset of those fields plus optional `broadcast: true` to send a blob transfer to all nodes. POST `/api/wifi_creds/reset` restores `secrets.h` defaults in RAM and NVS. Changes take effect on next reboot; use "Reboot All" button or `REBOOT_CMD 0xFF` to apply.
- **`index_html.h` is auto-generated.** The Makefile runs `minify_index_html.sh` before each compile, which minifies `index_html.h.bak` and writes `index_html.h`. Always edit `index_html.h.bak`; never edit `index_html.h` directly.
- **Rules NVS layout.** Namespace `"rules"`, keys `"r0"` through `"r<MAX_RULES-1>"`. Each key holds a 12-byte blob (`CanRule`). Empty/deleted rules have `trig_id == 0` and are skipped at evaluation time. Factory reset clears all keys and re-writes from `RULES_DEFAULT_INIT`.
- **Configurable CAN pins.** Default CAN TX/RX are GPIO 5/4. Override with `#define CAN_TX_PIN GPIO_NUM_x` and `#define CAN_RX_PIN GPIO_NUM_y` in a node config (used by the Cardputer which wires CAN to GPIO 1/2).
- **ESPNOW_ONLY mode.** Define `ESPNOW_ONLY` in a node config to skip SoftAP and web server setup entirely. The node still participates on ESP-NOW channel 6. `webui_tick()` and `webui_handle_node_cap()` are compiled out. Used by the Cardputer and headless nodes.
- **Channel capabilities.** `mod_channels` advertises what data channels a node publishes (e.g. VBAT, RPM, GPS). Define `CHAN_CAPS_INIT` in the node config using `CHAN_DEF()` macros. Responds to `CHAN_CAP_REQ (0x320)` with individual `CHAN_CAP (0x321)` frames per channel.
- **Cardputer upload via mass storage.** `make upload-cardputer` builds the firmware, copies `m5canoe.bin` to `$(CARDPUTER_VOLUME)/$(CARDPUTER_DIR)/` (defaults: `/Volumes/CARDPUTER/CANoE`), then calls `diskutil unmount`. The Cardputer must be mounted as a USB mass-storage drive (hold G0 at boot for UF2 mode). Override volume/path with `make upload-cardputer CARDPUTER_VOLUME=/Volumes/MYCARD CARDPUTER_DIR=firmware`.
- **Cardputer CLI bar status events.** `m5_set_event(const char*)` (declared in `mod_m5_cardputer.h`, stub when `ENABLE_M5_CARDPUTER` is not defined) writes a message to the CLI bar when it is in idle (non-CLI-active) state. Call it alongside `lcd_set_event()` for any event that should surface on both the LCD and the Cardputer. `m5_handle_frame()` also intercepts `RELAY_CMD` frames internally so relay events appear automatically.
- **Cardputer CLI history.** The CLI maintains an 8-entry ring buffer (`CLI_HIST_SZ`). In CLI mode, `;` scrolls to older commands and `.` scrolls to newer; pressing `.` past the newest restores the original draft. Typing any character or pressing del breaks out of history-browse mode and edits the currently shown text. Exact-duplicate consecutive entries are not stored.
- **Cardputer CAN vs FEED screens.** The CAN screen shows every frame as raw hex (`[source] ID b0 b1…`). The FEED screen shows only decodable frames in plain English (e.g. "SW1 pressed", "R2 ON", "RPM: 1450", "Batt: 12.45V"). Both buffers fill simultaneously regardless of which screen is active; switching screens shows the accumulated history. `cls` in the CLI clears whichever buffer the active screen displays. The FEED decoder covers: NODE_ANNOUNCE, BOOT_EVENT, BUS_ERROR, REBOOT_CMD, RELAY_CMD, RELAY_STATUS, SWITCH_EVENT, ENCODER_EVENT, TELEMETRY, ENGINE_DATA, GPS_DATA, WBO2_DATA, ECU_DATA, VIPER_CMD, CONFIG_WRITE. Unknown frame IDs are silently omitted from the FEED view.
- **Cardputer status bar selection mode.** Press `opt` to enter; initial selection is the screen-label item (index 6) so a single Enter opens the screen-switch dropdown. `;`/`.` navigate left/right through 7 items: relays R1–R6 (index 0–5) then the screen-label item (index 6). Enter or Space on a relay toggles it via `RELAY_CMD`. Enter on the screen label opens a 4-item dropdown (CAN / FEED / COMMANDS / SETTINGS); `;`/`.` navigate the dropdown, Enter switches to the selected screen, Del closes without switching. `opt` or Del exits the mode. In selection mode: selected relay = DARKGREY bg; unselected OFF relay = ACAPULCO_BLUE bg (blends with bar, outlined by WHITE border); unselected ON relay = GREEN. `fn` also exits by switching screens. `ctrl` (CLI toggle) is blocked while in selection mode.
- **Cardputer SETTINGS screen.** Three rows: **BT (relay node)** sends `CONFIG_WRITE` to `CFG_TARGET_RELAY_CTRL` with `CFG_KEY_BT_ENABLED`; **WiFi (all nodes)** broadcasts `CFG_KEY_WIFI_ENABLED` (reboots affected nodes); **Speaker volume** cycles MUTE/LOW/MED/HIGH/MAX and persists to Preferences namespace `"m5card"` key `"spk_vol"`. `;`/`.` navigate, Enter toggles/cycles, Del returns to CAN. Last-sent BT/WiFi state is shown but not read back from the bus (informational only).
- **Fuel pump safety FSM.** `mod_fuel_pump` on the relay controller owns the fuel pump relay (`FUEL_PUMP_RELAY`, default R1). States: **PRIME** (pump ON for `FUEL_PUMP_PRIME_MS` after boot) → **ARMED** (pump OFF, waiting for the configured gates to all pass) → **RUNNING** (pump ON; if any enabled gate goes stale for `FUEL_PUMP_STALL_MS`, falls back to **ARMED**). **Multi-gate**: each gate tracks its own "last seen fresh" timestamp. The RPM gate is driven by `ENGINE_DATA (0x304)` from any node. The COIL gate is driven by the local coil ADC sampled inside this module (`IGN_COIL_ADC_PIN`, hysteresis between `IGN_COIL_ON/OFF_THRESHOLD_CV`) — there is no CAN round trip for the COIL signal. `IGNITION_DATA (0x310)` is still broadcast by `mod_fuel_pump` on coil edges + every ~1 s as a heartbeat for observability (Cardputer FEED decodes it), but the FSM does not consume the frame. The coil sensing was folded in from the former `mod_ignition` module — that producer/consumer pair only existed on the same node and the round trip added no decoupling. The current mode is a bitmask — bit 0 = RPM gate enabled, bit 1 = COIL gate enabled. `gates_permit(now)` returns true iff every enabled gate has been fresh within `FUEL_PUMP_STALL_MS`. Mode 0 (OFF) bypasses everything and forces the pump on; mode 3 (BOTH = default on the relay node) is strictest because either gate alone catches the failure mode that originally burned out the pump (master switch on / key out → no spark **and** coil unpowered). **Advisory, not authoritative**: the FSM only emits a `RELAY_CMD` on state transitions, so manual relay toggles (web UI, Cardputer, rules) still work and the FSM re-asserts on the next transition. The boot rule `TRIG_BOOT → ACT_RELAY_ON(0)` was removed from `relay_controller.h` because PRIME entry already turns the pump on. **Override**: `CFG_KEY_FUEL_PUMP_SAFETY (0x61)` or `ACT_FUEL_PUMP_SAFETY_*` (arg0 = mode bitmask). State is RAM-only — every reboot restores `FUEL_PUMP_DEFAULT_MODE`. On mode change, the FSM restarts from PRIME so the carb bowl is primed before the next gate check; if the engine is already running, the ~500 ms ARMED gap is bridged by carb bowl fuel. **Audible alerts**: on stall (`RUNNING → ARMED` with reason=4), the FSM broadcasts `BUZZER_CMD` (`BUZZER_SEQ_FUEL_PUMP_OFF`, 4-pulse low/high) + `CAN_ID_FUEL_PUMP_STATE (0x311)`. The switch panel's `mod_buzzer` plays the sequence; the Cardputer's `m5_handle_frame` routes the same `BUZZER_CMD` to `m5_beep_alert()` + a "FUEL PUMP CUT" CLI-bar event. Adding new gates (e.g. oil pressure as bit 2) requires: a new frame ID, an updated `gate_*_ok()` helper, and extending the `gates_permit` mask check — the bit-flag composition extends to 8 gates in a single byte. Oil pressure specifically will need a per-gate grace period right after PRIME because oil pressure ramps over 1-2 seconds after cranking starts.
- **Cardputer OTA screen.** Lists `.bin` files from `/canoe-firmwares/` on the MicroSD; `;`/`.` navigate, Enter picks → confirm prompt → Enter again uploads, Del cancels. Upload flow: `WiFi.begin(AP_SSID, AP_PASSWORD)` (radio already in `WIFI_STA` for ESP-NOW so no mode change), wait up to 15 s for `WL_CONNECTED`, open `WiFiClient` to `192.168.4.1:80`, stream a `multipart/form-data` body to `POST /api/ota` (field name `firmware`, boundary `----CANoEBoundary8273`), parse the response status line for ` 200 `, then `WiFi.disconnect(false, true)` + `esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE)` to restore the ESP-NOW channel. SD is mounted lazily on first OTA entry and the mount persists across uploads via `g_sd_mounted`. **Caveat:** all nodes use the same SSID, so only one target should be powered on during upload — there is no per-node addressing yet (see roadmap).
- **Unified error system (`mod_error`).** Any module can call `error_raise_local(code, severity, arg0)` to broadcast an `ERROR_EVENT (0x0F6)` frame. The receiver side (`error_handle_frame()` in the frame dispatch loop) maps severity to hardware: WARNING = `buzzer_alert()` + LED flash; CRITICAL = same with latching; EMERGENCY = `buzzer_fuel_pump_off()` (loudest alarm). The Cardputer reacts via `m5_beep_alert()` + CLI-bar event text. Non-latching errors auto-clear after `ERR_AUTO_CLEAR_MS` (default 10 s); latching errors require an explicit `error_clear(code)`. Rate-limited to one frame per error code per second. Active errors are tracked in an 8-slot table queryable via `error_any_active()`, `error_is_active()`, `error_max_severity()`. No feature flag gate — always compiled in. Modules that emit errors: `mod_relay` (watchdog), `mod_fuel_pump` (stall), `mod_battery` (low voltage with hysteresis), `mod_wbo2` (sensor fault), `mod_ecu` (injector saturated, NTC sensor fault), `mod_switches` (ACK timeout), `bus.cpp` (CAN bus errors, ESP-NOW init failure), `mod_wifi_creds` (credential rejection), `canoe.ino` (relay confirm timeout).

## Known TODO list (in rough priority order)

1. ~~Add a password to the SoftAP before any field install~~ — **done** (`secrets.h` pattern; AP_SSID/AP_PASSWORD gitignored).
2. ~~Encrypt ESP-NOW~~ — **done** (PMK at init, dynamic encrypted unicast peers, broadcast retained for discovery).
3. ~~Dashboard panel in the web UI~~ — **done** (Control tab: switch state, buttons, LEDs, relay tiles on relay_controller, viper controls on viper_interface). Still missing: battery voltage graph from TELEMETRY (0x300) frames.
4. Python `can-gw` utility so a Mac with a CANable/MCP2515 USB adapter can be a bus participant for scripting and logging.
5. Low-voltage cutoff in `mod_relay.cpp` — when `vbat_cv` drops below a configurable threshold, force non-essential relays off.
6. Switch from polling to SSE or WebSocket for the frame log (lower latency, less traffic).
7. ~~ESP-NOW ack/retry for switch events~~ — **done** (`SWITCH_ACK 0x202`, 3 retries at 80 ms, buzzer + LED flash on timeout).
8. ~~Bridge node joining home WiFi~~ — **done** (bridge.h, BRIDGE_MODE, STA_SSID/STA_PASSWORD, NODE_CAP discovery, MQTT via mod_mqtt).
9. Auto-unify: firmware recognizes a "factory_wiring_trusted" flag in NVS and drops the isolation (longer term, once the owner trusts the factory harness).
10. Cardputer OTA: per-node SSID disambiguation so other nodes don't have to be powered off. Two candidate approaches: (a) a new CAN command (e.g. `OTA_PREP 0xF6 [target_node_id, dur_secs]`) that tells the target to swap its SoftAP SSID to something unique (e.g. `AccessoryBus-<node_id>`) for N seconds, then revert; (b) an "OTA inhibit" CAN broadcast that puts non-targets into a state where they refuse `/api/ota` POSTs. Approach (a) keeps the Cardputer flow simple (just pick the unique SSID before `WiFi.begin`) but requires the target to be ESP-NOW-reachable so it hears the command. Approach (b) needs new gating in `webui.cpp::handle_ota_upload`.
11. Cardputer OTA: optional MD5 verification — compute MD5 while streaming the file, send as `X-MD5` header, have `webui.cpp::handle_ota_upload` validate after `Update.end()`. Catches SD bit-rot and SPI transfer errors before the target reboots.

## iOS Tuner app (`tuner/`)

Native iOS companion app (iOS 17+, Swift/SwiftUI, no third-party dependencies). Connects to any CANoE node via BLE or Wi-Fi and provides live telemetry, data logging, and GPS injection.

### Architecture

- **`TunerApp.swift`** — `@main`. Creates an `AppSettings` `@StateObject` and injects it as an `@EnvironmentObject` into the entire SwiftUI tree.
- **`Theme.swift`** — Single source of truth for appearance:
  - `Color.acapulcoBlue` — brand primary (`#006DAC`), used for all accent/tint throughout both light and dark mode.
  - `AppSettings` — `ObservableObject` with five `@AppStorage` font-scale multipliers (gauge, label, data, axis, caption). Views consume these via `@EnvironmentObject`.
  - `AppearanceSheet` — presented from the dashboard toolbar (`textformat.size` icon); live-preview sliders (0.7×–1.8×) per font category, Reset to Defaults.
- **`ContentView.swift`** — Root router. `ConnectView` handles BLE scan, Wi-Fi IP entry, and simulator launch. `NativeDashboardView` owns the tab bar (Frames / Logger) and toolbar.
- **`LogView.swift`** — Logger tab. Live gauge grid (`GaugeTile`), line graph panels (`GraphPanel`), Braun analog dial panels (`BraunGaugeView`), session stats. All font sizes pull from `AppSettings`.
- **`DataLogger.swift`** — Session recording, CSV export, `ChannelDescriptor` registry, dynamic decoder for `CHAN_CAP (0x321)` frames.
- **`BLEManager.swift`** — CoreBluetooth central. Auto-reconnects to saved peripheral UUID on every launch. Parses `[id_lo, id_hi, dlc, d0..d7]` wire format.

### Theme conventions

- Never hard-code a tint color. Use `.tint(.acapulcoBlue)` for primary actions and `.tint(.secondary)` for destructive/neutral ones (`.tint(.red)` for the Disconnect button is the only exception).
- Font sizes come from `AppSettings` helpers (`gaugeFont()`, `labelFont()`, `dataFont()`, `axisFont()`, `captionFont()`). Hard-coded `size:` values are only acceptable inside `Canvas` blocks where environment objects aren't accessible — pass `fontScale`/`axisScale` as plain properties in that case (see `BraunGaugeView`).
- CAN frame log: TX rows use `Color.acapulcoBlue`, RX rows use `Color.secondary`.
- New source files must be added to both `Tuner/` directory and `Tuner.xcodeproj/project.pbxproj` (PBXBuildFile, PBXFileReference, Tuner group children, and Sources build phase).

## Preferences

Owner works primarily in JavaScript / VueJS / Quasar and Python, on a Mac. When the UI grows beyond what vanilla JS is comfortable for, the natural next step is a separate Quasar app hosted off-device that talks to the ESP32 JSON API — keeping the on-device UI minimal and offline-capable while the "desktop" UI gets the rich framework treatment.

## Useful one-liners

```bash
# Compile all three nodes in sequence
make all

# Tail all three serial ports at once (requires `brew install tmux`)
tmux new-session \; \
  send-keys 'arduino-cli monitor -p /dev/cu.usbserial-XXXX -c baudrate=115200' C-m \; \
  split-window -h \; send-keys 'arduino-cli monitor -p /dev/cu.usbserial-YYYY -c baudrate=115200' C-m \; \
  split-window -v \; send-keys 'arduino-cli monitor -p /dev/cu.usbserial-ZZZZ -c baudrate=115200' C-m
```

## Quick mental model

- Switch is pressed on switch_panel → `mod_switches` debounces and emits `SWITCH_EVENT (0x200)` via `bus_tx()` → frame is self-echoed back into rx ring → `rules_handle_frame()` in `mod_rules` matches a rule → rule executes (e.g. `RELAY_CMD`) via `bus_tx()` → relay_controller `bus_rx()` returns the frame → `relay_handle_frame()` flips GPIO → `send_relay_status()` broadcasts new state → switch_panel's `g_relay_mirror` stays in sync → web UI on either node logs every frame in real time.
- Rules are stored in NVS and evaluated on every incoming (including self-echoed) CAN frame. A rule fires when its `trig_id` matches and both byte conditions pass. Action is dispatched immediately inline.
- Config changes: UI or external tool sends `CONFIG_WRITE` → target node's module handler updates RAM, optionally persists to NVS, echoes a `CONFIG_READ_RESP` → sender sees confirmation.
- Viper alarm command: any node (or the web UI) sends `VIPER_CMD (0x510)` with a 1-byte command code → viper_interface `bus_rx()` returns the frame → `viper_handle_frame()` calls the appropriate ViperESP2 method → ViperESP2 writes the 5-byte serial packet to the alarm over UART2 → alarm responds → `on_viper_message()` callback fires → viper_interface calls `bus_tx(VIPER_STATUS)` → all nodes and the web UI log the raw alarm response.
