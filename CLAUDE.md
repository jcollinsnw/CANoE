# Antique Car CAN Bus Accessory System

A parallel 12V accessory wiring system for an antique car, built around four ESP32 nodes on a shared CAN bus with ESP-NOW wireless fallback and a self-hosted web console. This file exists to onboard future Claude sessions quickly — read it top to bottom before making changes.

## Project goals (from the owner)

- Keep the factory wiring completely isolated while a parallel accessory system runs on its own battery, fuse box, and charging path.
- Down the road, unify the two systems once the factory wiring is vetted.
- All accessory loads driven through a 6-relay / 6-fuse box.
- CAN bus between a switch panel (buttons/toggles) and the relay controller.
- CAN-frame-triggered rules engine for flexible switch→relay/LED/alarm mappings, stored in NVS and editable at runtime.
- Web console for debugging, direct CAN frame injection, and rules management.
- WiFi fallback if the CAN wire ever fails.

## Repo layout

```
.
├── CLAUDE.md                                   # this file
├── Makefile                                    # configure + compile/upload
├── wiring-with-transceivers.svg                # production wiring diagram
├── wiring-bench-mode.svg                       # bench wiring (no transceivers)
└── firmware/
    ├── configs/                                # one header per physical node
    │   ├── relay_controller.h
    │   ├── switch_panel.h
    │   ├── viper_interface.h
    │   └── ecu_node.h
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
        ├── mod_viper.h / mod_viper.cpp         # Viper 5305V serial bridge (ViperESP2 inlined)
        ├── mod_mpu6050.h / mod_mpu6050.cpp     # MPU-6050 accelerometer / shake detection
        ├── mod_dht22.h / mod_dht22.cpp         # AM2302 temperature/humidity sensor
        ├── mod_rpm.h / mod_rpm.cpp             # engine RPM via PC817C optocoupler + interrupt counting
        ├── mod_wbo2.h / mod_wbo2.cpp           # wideband O2 sensor analog read + WBO2_DATA broadcast
        ├── mod_ecu.h / mod_ecu.cpp             # dual-mode fuel controller (carb PI + TBI injection)
        ├── mod_gps.h / mod_gps.cpp             # GPS speed/heading via NMEA UART (u-blox Neo-6M/8M)
        └── mod_mqtt.h / mod_mqtt.cpp           # MQTT bridge: publishes CAN frames, subscribes for injection (bridge only)
```

**How it works:** `firmware/configs/<node>.h` defines which feature flags (`ENABLE_RELAY`, `ENABLE_LCD`, etc.) and pin assignments apply to that physical ESP32. The Makefile copies the right config to `node_config.h` before compiling, so the single sketch folder produces the correct firmware for each node. Every module's `.cpp` wraps its entire body in `#ifdef ENABLE_*` so unneeded modules compile to nothing.

**Adding a new node:** create `firmware/configs/<new_node>.h` with the desired `ENABLE_*` flags and pin assignments, then add a target in the Makefile that copies it and compiles.

## Architecture overview

Four ESP32 nodes, each runs up to four things concurrently:

1. **TWAI** (ESP32's CAN controller) — primary bus transport at 125 kbit/s.
2. **ESP-NOW** — secondary transport, broadcasts every frame to all peers on the same WiFi channel. Works as a failover if the wire breaks.
3. **SoftAP** — SSID `AccessoryBus`, fixed channel 6, so phones roam to whichever node is closer and ESP-NOW peers find each other regardless of which AP the user is on.
4. **HTTP + DNS captive portal** — serves a terminal-style web UI at `http://192.168.4.1` with a CAN command line, live frame log, node status, and a "force WiFi-only" toggle for testing the fallback path.

`bus.cpp` is the transport abstraction. Application code never calls `twai_transmit` or `esp_now_send` directly — it calls `bus_tx()`, which fans out to both transports, and `bus_rx()`, which returns frames from either with (node_id, seq) dedup.

## Node identities

| Node             | NODE_ID | Config file                      | Features                                                                    |
|------------------|---------|----------------------------------|-----------------------------------------------------------------------------|
| switch_panel     | 0x01    | configs/switch_panel.h           | ENABLE_SWITCHES, ENABLE_RULES, ENABLE_LCD, ENABLE_MENU, ENABLE_BUZZER, ENABLE_LEDS |
| relay_controller | 0x02    | configs/relay_controller.h       | ENABLE_RELAY, ENABLE_RULES, ENABLE_RPM                                      |
| viper_interface  | 0x03    | configs/viper_interface.h        | ENABLE_VIPER, ENABLE_LCD, ENABLE_MPU6050                                    |
| ecu_node         | 0x04    | configs/ecu_node.h               | ENABLE_RPM, ENABLE_WBO2, ENABLE_ECU (MAP, TPS, CLT, IAT, carb solenoid, dual injectors) |
| bridge           | 0x05    | configs/bridge.h                 | BRIDGE_MODE — wired CAN + SoftAP + STA to home router; no ESP-NOW. Aggregated node discovery web UI. Optional MQTT publishing via mod_mqtt. |

## Feature flags (defined in configs/*.h)

| Flag              | Module              | Description                                                          |
|-------------------|---------------------|----------------------------------------------------------------------|
| `ENABLE_RELAY`    | mod_relay           | 6 relay GPIO outputs, safety watchdog, battery ADC                   |
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
| `ENABLE_GPS`      | mod_gps             | GPS speed/heading via NMEA UART; `GPS_SERIAL_NUM`, `GPS_RX_PIN`, `GPS_TX_PIN`, `GPS_BAUD` |
| `ENABLE_WBO2`     | mod_wbo2            | Wideband O2 analog read (0–5V controller output); `WBO2_PIN`, `WBO2_SAMPLE_MS`, `WBO2_MIN/MAX_V`, `WBO2_MIN/MAX_AFR` |
| `ENABLE_ECU`      | mod_ecu             | Dual-mode fuel controller: Mode 0 = carb air-bleed solenoid (LEDC PWM PI loop); Mode 1 = TBI dual injectors (esp_timer pulse). Reads MAP, TPS, CLT, IAT. VE table + STFT closed-loop from WBO2_DATA. |
| `BRIDGE_MODE`     | (webui + mod_mqtt)  | Bridge node: SoftAP + wired CAN + WiFi STA to home router. No ESP-NOW. Web UI shows aggregated control panel built from NODE_CAP discovery. Enables `/api/nodecaps` endpoint. Requires `STA_SSID`/`STA_PASSWORD`. |
| `MQTT_BROKER`     | mod_mqtt            | Enable MQTT publishing on the bridge. Set to broker IP/hostname. Requires PubSubClient library. Publishes all CAN frames to `{MQTT_TOPIC_PREFIX}/frames`; subscribes to `{MQTT_TOPIC_PREFIX}/send` for injection. |

`ENABLE_MENU` subflags (defined alongside `ENABLE_MENU` in the node config):

| Flag               | Submenu compiled in                        |
|--------------------|--------------------------------------------|
| `MENU_HAS_RELAYS`  | Relay toggle submenu (6 relays)            |
| `MENU_HAS_VIPER`   | Viper lock/unlock/start submenu            |
| `MENU_HAS_BUS`     | Live TWAI health counter display           |
| `MENU_HAS_DISPLAY` | LCD backlight toggle                       |
| `MENU_HAS_WIFI`    | Per-node WiFi enable/disable (sends CONFIG_WRITE; self-toggle restarts) |

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
```

**Action macros:**
```c
ACT_RELAY_TOGGLE(r)      ACT_RELAY_ON(r)       ACT_RELAY_OFF(r)
ACT_ALL_OFF()            ACT_LED_ON(node,led)  ACT_LED_OFF(node,led)
ACT_WIFI_ENABLE(node)    ACT_WIFI_DISABLE(node)
ACT_VIPER(cmd)           ACT_MENU_SELECT()     ACT_MENU_ENTER()
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

**Runtime editing:** `/api/rules` REST endpoints (GET / POST / DELETE) and a **Rules** tab in the web UI allow viewing, creating, editing, and deleting rules at runtime. Changes are persisted to NVS via `rules_set()`. A factory reset endpoint (`POST /api/rules/reset`) restores `RULES_DEFAULT_INIT`.

## Switch module

`mod_switches` is now **input-only**. It polls GPIO state, debounces, and publishes:
- `SWITCH_EVENT (0x200)` — `[switch_id, event]` where event is 0 RELEASE, 1 PRESS, 2 LONG_PRESS, 3 DOUBLE_PRESS.
- `ENCODER_EVENT (0x201)` — `[event, count]` where event is 0 CW, 1 CCW, 2 PRESS, 3 RELEASE, 4 LONG_PRESS.

It has no action dispatch and no NVS config. All switch→action behavior lives in the rules engine. Encoder scroll-in-menu is handled directly in `accessory_node.ino`'s `bus_rx()` loop (encoder events are self-echoed so they appear in rx just like any other frame).

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
| GPIO 34   | Relay controller: battery voltage ADC (optional)               |
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
| 0x100  | RELAY_CMD         | `[mask, state]` — only bits set in mask are applied    |
| 0x101  | RELAY_STATUS      | `[bitmap]` — broadcast at 5 Hz                         |
| 0x102  | LED_CMD           | `[target_node_id, mask, state]` — any node → target; 0xFF target = broadcast |
| 0x103  | LED_STATUS        | `[node_id, bitmap]` — sent by target on change         |
| 0x200  | SWITCH_EVENT      | `[switch_id, SwitchEvent]`                             |
| 0x201  | ENCODER_EVENT     | `[EncoderEvent, count]`                                |
| 0x300  | TELEMETRY         | `[vbat_cv_lo, vbat_cv_hi, i_da_lo, i_da_hi, vsol_cv_lo, vsol_cv_hi, flags, _]` |
| 0x301  | ENV_DATA          | `[temp_d1_lo, temp_d1_hi, humi_d1_lo, humi_d1_hi]` (0.1°C, 0.1%) |
| 0x302  | IMU_DATA          | `[accel_x_lo, accel_x_hi, accel_y_lo, accel_y_hi, accel_z_lo, accel_z_hi]` |
| 0x303  | SHAKE_EVENT       | `[magnitude, axis_mask]`                               |
| 0x304  | ENGINE_DATA       | `[rpm_lo, rpm_hi]` — uint16 LE RPM; broadcast at `RPM_SAMPLE_MS` interval |
| 0x305  | GPS_DATA          | `[speed_lo, speed_hi, heading_lo, heading_hi, flags]` — speed 0.1 mph, heading 0.1 deg, flags: bit0=fix, bit1=speed valid, bit2=heading valid |
| 0x306  | WBO2_DATA         | `[afr_lo, afr_hi]` — uint16 LE, AFR × 100 (e.g. 1470 = 14.70 AFR)                                                                             |
| 0x307  | ECU_DATA          | `[mode, map_kpa, tps_pct, clt_enc, iat_enc, pw_lo, pw_hi, flags]` — mode: 0=carb 1=inject; temps encoded as °C+40; pw = duty×100 (carb) or μs (inject); flags: bit0=closed_loop, bit1=enriching, bit2=inj_saturated, bit3=running |
| 0x308  | ECU_CMD           | `[cmd, arg0, arg1, arg2]` — cmd: 0x01 set mode (arg0: 0/1), 0x02 set target AFR×100 (arg1+arg2 uint16 LE), 0x03 fuel cut (arg0: 0/1), 0x04 reset trim |
| 0x400  | CONFIG_WRITE      | `[target, key, index, _reserved, arg, arg2_lo, arg2_hi, flags]` |
| 0x401  | CONFIG_READ_REQ   | `[target, key, index]` (index 0xFF = all)              |
| 0x402  | CONFIG_READ_RESP  | same layout as CONFIG_WRITE (flags byte unused)        |
| 0x403  | CONFIG_SAVE       | `[target, action]` — 0x01 commit, 0x02 reload, 0x03 factory reset |
| 0x500  | LCD_CMD           | `[row, col, char…]` or `[0xFF]` clear — any node → switch_panel |
| 0x510  | VIPER_CMD         | `[cmd]` — any node → viper_interface; cmd: 0x01 lock, 0x02 unlock, 0x03 remote start |
| 0x511  | VIPER_STATUS      | `[b0..b4]` — viper_interface → everyone; raw 5-byte Viper alarm packet |

Config targets: `0x01 SWITCH_PANEL`, `0x02 RELAY_CTRL`, `0x03 VIPER`, `0x04 ECU`, `0x05 BRIDGE`, `0xFF BROADCAST`.

Config keys:
- `0x01 CFG_KEY_NODE_ID` — reassign node_id; arg (data[4]) = new ID (0x01–0xFE); saves to NVS and restarts. Broadcast target **not accepted**.
- `0x20 CFG_KEY_RELAY_MAX_ON_MS` — per-relay safety auto-off timeout (arg2_lo/hi = ms, 0 = no limit; index = relay 0–5)
- `0x30 CFG_KEY_WIFI_ENABLED` — legacy: sets both ap_en and espnow_en; arg=0/1; node restarts
- `0x31 CFG_KEY_AP_ENABLED` — enable/disable SoftAP + web server; arg=0/1; node restarts
- `0x32 CFG_KEY_ESPNOW_ENABLED` — enable/disable ESP-NOW radio; arg=0/1; node restarts
- `0x40 CFG_KEY_RPM_REDLINE` — RPM redline for display widget (arg2_lo/hi = uint16 RPM)
- `0x51 CFG_KEY_ECU_MODE` — 0=carb, 1=inject; arg[4]=value; persisted to NVS
- `0x52 CFG_KEY_ECU_TARGET_AFR` — target AFR × 100 (uint16 LE in arg2_lo/hi)
- `0x53 CFG_KEY_ECU_BASE_PW` — injection base pulse width μs at 100% VE, 100 kPa (uint16 LE)

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

Everything is in the Arduino-ESP32 core except the bridge node's MQTT module.

- `driver/twai.h` — CAN controller
- `WiFi.h` / `esp_now.h` — wireless
- `WebServer.h` / `DNSServer.h` — HTTP + captive portal
- `Preferences.h` — NVS persistence
- `PubSubClient` — MQTT client (bridge only, when `MQTT_BROKER` is defined); install with `arduino-cli lib install "PubSubClient"`

Tested against Arduino-ESP32 **v2.x and v3.x**. The ESP-NOW receive callback signature changed in v3.x (`esp_now_recv_info_t*` first arg instead of `const uint8_t* mac`); `bus.cpp` uses a version preprocessor guard to handle both. TWAI APIs also changed in v3.x — if bumping, verify compilation before committing.

## Build & flash (Mac)

Using arduino-cli:

```bash
make relay                                         # compile for relay controller
make switch                                        # compile for switch panel
make viper                                         # compile for viper interface
make ecu                                           # compile for ECU node
make bridge                                        # compile for bridge node
make all                                           # compile all five in sequence
make upload-switch  PORT=/dev/cu.usbserial-XXXX
make upload-relay   PORT=/dev/cu.usbserial-YYYY
make upload-viper   PORT=/dev/cu.usbserial-ZZZZ
make upload-ecu     PORT=/dev/cu.usbserial-WWWW
make upload-bridge  PORT=/dev/cu.usbserial-VVVV
make monitor        PORT=/dev/cu.usbserial-XXXX
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
- **Settings** (hamburger menu) — node ID reassignment (saves to NVS, restarts).
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
Check "force wifi-only" in the header. `bus_tx()` stops using TWAI; everything should still work via ESP-NOW. Uncheck to restore wire.

## Conventions and gotchas

- **Init order.** In `setup()` the order must be `relay_setup()` → `setup_can()` → `webui_init()` → `bus_init()`. `webui_init` brings up WiFi so `esp_now_init` has a radio to bind to; `bus_init` requires WiFi ready. Relay pins are initialized first so outputs are known-good before any CAN traffic.
- **Frame-flow direction.** Application code only goes through the bus. Do not call `twai_transmit` directly except inside `bus.cpp`; it will bypass ESP-NOW and the web UI log.
- **Self-echo.** `bus_tx()` feeds every outbound frame back into the RX ring (tagged `source="self"`). This means state mirrors, LCD updates, buzzer, and rules all react to web-UI-injected frames exactly the same as frames from other nodes. Don't be surprised when you see your own TX come back through `bus_rx()`.
- **Dedup.** ESP-NOW frames carry their own `(node_id, seq)` header; a 2-second window filters repeats. CAN frames are canonical and always passed through. This is safe because our application messages are idempotent (RELAY_CMD, STATUS, TELEMETRY) or edge-triggered with short windows (SWITCH_EVENT).
- **Captive-portal probes.** iOS and Android each hit different URLs to detect captive portals — we catch the common ones in `webui.cpp` and bounce them to `/`.
- **Open AP.** Fine in a garage, risky in public. Set `AP_PASSWORD` in each WiFi node's config header (`switch_panel.h`, `viper_interface.h`) before the car leaves the driveway. Must be ≥ 8 chars for WPA2 and identical on every node. `AP_SSID` and `AP_HIDDEN` are also configurable there.
- **Horn safety.** `RELAY_MAX_ON_INIT` in `configs/relay_controller.h` caps relay 5 (horn) at 30s. HOLD-style rules (SW_PRESS→relay ON, SW_RELEASE→relay OFF) still rely on the relay controller's watchdog as a backstop.
- **ADC calibration.** GPIO 34 on the relay controller is read with default attenuation; `VBAT_DIVIDER_RATIO` in `configs/relay_controller.h` assumes 10k + 2.2k divider. Re-tune to your resistors before trusting the telemetry.
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
- **Buzzer sequencer.** `mod_buzzer` uses Arduino `tone()`/`noTone()` to drive a passive piezo — no LEDC setup required. Sequences are arrays of `{freq, ms}` notes advanced by `buzzer_tick()` each loop; a new `play_seq()` call immediately interrupts any active sequence. Relay sounds fire on RELAY_CMD frames (including self-echoed ones from the web UI), so every relay state change produces feedback regardless of its source. `BUZZER_PIN` must not be an input-only GPIO (avoid 34/35).
- **Rules NVS layout.** Namespace `"rules"`, keys `"r0"` through `"r<MAX_RULES-1>"`. Each key holds a 12-byte blob (`CanRule`). Empty/deleted rules have `trig_id == 0` and are skipped at evaluation time. Factory reset clears all keys and re-writes from `RULES_DEFAULT_INIT`.

## Known TODO list (in rough priority order)

1. Add a password to the SoftAP before any field install (`AP_PASSWORD` in each node's config header — must match on all WiFi nodes).
2. Encrypt ESP-NOW (`esp_now_set_pmk`, mark broadcast peer `encrypt = true`).
3. ~~Dashboard panel in the web UI~~ — **done** (Control tab: switch state, buttons, LEDs, relay tiles on relay_controller, viper controls on viper_interface). Still missing: battery voltage graph from TELEMETRY (0x300) frames.
4. Python `can-gw` utility so a Mac with a CANable/MCP2515 USB adapter can be a bus participant for scripting and logging.
5. Low-voltage cutoff in `mod_relay.cpp` — when `vbat_cv` drops below a configurable threshold, force non-essential relays off.
6. Persist the "force wifi-only" flag across reboots (currently RAM-only).
7. Switch from polling to SSE or WebSocket for the frame log (lower latency, less traffic).
8. ESP-NOW ack/retry for switch events specifically (they're the only edge-triggered frames).
9. ~~Bridge node joining home WiFi~~ — **done** (bridge.h, BRIDGE_MODE, STA_SSID/STA_PASSWORD, NODE_CAP discovery, MQTT via mod_mqtt).
10. Auto-unify: firmware recognizes a "factory_wiring_trusted" flag in NVS and drops the isolation (longer term, once the owner trusts the factory harness).

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
