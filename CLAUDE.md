# Antique Car CAN Bus Accessory System

A parallel 12V accessory wiring system for an antique car, built around three ESP32 nodes on a shared CAN bus with ESP-NOW wireless fallback and a self-hosted web console. This file exists to onboard future Claude sessions quickly — read it top to bottom before making changes.

## Project goals (from the owner)

- Keep the factory wiring completely isolated while a parallel accessory system runs on its own battery, fuse box, and charging path.
- Down the road, unify the two systems once the factory wiring is vetted.
- All accessory loads driven through a 6-relay / 6-fuse box.
- CAN bus between a switch panel (buttons/toggles) and the relay controller.
- Reconfigurable switch → relay mappings and per-relay safety behavior at runtime.
- Web console for debugging and direct CAN frame injection.
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
    │   └── viper_interface.h
    └── accessory_node/                         # single unified sketch (all nodes)
        ├── accessory_node.ino                  # setup() / loop() / frame dispatch
        ├── node_config.h                       # ← overwritten by Makefile before each build
        ├── node_state.h                        # extern g_relay_mirror, g_can_ok, g_menu_active
        ├── can_protocol.h                      # CAN message IDs, enums, pack/unpack helpers
        ├── bus.h / bus.cpp                     # dual-transport abstraction (TWAI + ESP-NOW)
        ├── webui.h / webui.cpp                 # SoftAP + captive portal + HTTP + JSON API
        ├── index_html.h                        # embedded single-page web UI
        ├── mod_relay.h / mod_relay.cpp         # relay GPIO, watchdog, telemetry
        ├── mod_lcd.h / mod_lcd.cpp             # HD44780 16×2 driver (PCF8574 I2C backpack)
        ├── mod_switches.h / mod_switches.cpp   # switch inputs, encoder, LCD menu
        ├── mod_viper.h / mod_viper.cpp         # Viper 5305V serial bridge (ViperESP2 inlined)
        ├── mod_mpu6050.h / mod_mpu6050.cpp     # MPU-6050 accelerometer / shake detection
        └── mod_dht22.h / mod_dht22.cpp         # AM2302 temperature/humidity sensor
```

**How it works:** `firmware/configs/<node>.h` defines which feature flags (`ENABLE_RELAY`, `ENABLE_LCD`, etc.) and pin assignments apply to that physical ESP32. The Makefile copies the right config to `node_config.h` before compiling, so the single sketch folder produces the correct firmware for each node. Every module's `.cpp` wraps its entire body in `#ifdef ENABLE_*` so unneeded modules compile to nothing.

**Adding a new node:** create `firmware/configs/<new_node>.h` with the desired `ENABLE_*` flags and pin assignments, then add a target in the Makefile that copies it and compiles.

## Architecture overview

Three ESP32 nodes, each runs up to four things concurrently:

1. **TWAI** (ESP32's CAN controller) — primary bus transport at 125 kbit/s.
2. **ESP-NOW** — secondary transport, broadcasts every frame to all peers on the same WiFi channel. Works as a failover if the wire breaks.
3. **SoftAP** — SSID `AccessoryBus`, fixed channel 6, so phones roam to whichever node is closer and ESP-NOW peers find each other regardless of which AP the user is on.
4. **HTTP + DNS captive portal** — serves a terminal-style web UI at `http://192.168.4.1` with a CAN command line, live frame log, node status, and a "force WiFi-only" toggle for testing the fallback path.

`bus.cpp` is the transport abstraction. Application code never calls `twai_transmit` or `esp_now_send` directly — it calls `bus_tx()`, which fans out to both transports, and `bus_rx()`, which returns frames from either with (node_id, seq) dedup.

## Node identities

| Node             | NODE_ID | Config file                      | Features                                         |
|------------------|---------|----------------------------------|--------------------------------------------------|
| switch_panel     | 0x01    | configs/switch_panel.h           | ENABLE_SWITCHES, ENABLE_LCD, ENABLE_DHT22        |
| relay_controller | 0x02    | configs/relay_controller.h       | ENABLE_RELAY                                     |
| viper_interface  | 0x03    | configs/viper_interface.h        | ENABLE_VIPER, ENABLE_LCD, ENABLE_MPU6050         |

## Feature flags (defined in configs/*.h)

| Flag              | Module              | Description                                           |
|-------------------|---------------------|-------------------------------------------------------|
| `ENABLE_RELAY`    | mod_relay           | 6 relay GPIO outputs, safety watchdog, battery ADC    |
| `ENABLE_SWITCHES` | mod_switches        | 10 inputs (6 latching + 4 buttons), encoder, menu     |
| `ENABLE_LCD`      | mod_lcd             | HD44780 16×2 via PCF8574 I2C backpack                 |
| `ENABLE_VIPER`    | mod_viper           | Viper 5305V serial bridge over UART2                  |
| `ENABLE_MPU6050`  | mod_mpu6050         | MPU-6050 shake detection, IMU data broadcast          |
| `ENABLE_DHT22`    | mod_dht22           | AM2302 temperature/humidity broadcast                 |

## Pinout

All nodes share the same CAN pins. Node-specific pins are defined in the config header.

| Pin       | Role                                                           |
|-----------|----------------------------------------------------------------|
| GPIO 5    | CAN TX (all nodes — to transceiver TXD, or shared bus in bench mode) |
| GPIO 4    | CAN RX (all nodes — from transceiver RXD, or shared bus)      |
| GPIO 16–22| Relay controller: ULN2803 inputs (relays 1–6)                  |
| GPIO 25,26,27,32,33,13 | Switch panel: switches to GND                    |
| GPIO 34, 35 | Switch panel: rotary encoder GA (CLK/A) and GB (DT/B). Module is CJMCU-111 (EC11-based). Has onboard 3.3 kΩ pull-ups (marked 332) — connect module VCC to 3V3, no external resistors needed. The shaft physically clicks but the SW contact is not wired to any pin header on the CJMCU-111 PCB — use a dedicated panel button (BTN1–4) for encoder select/back instead. |
| GPIO 34   | Relay controller: battery voltage ADC (optional)               |
| GPIO 16   | Viper interface: UART2 TX → level shifter → Viper serial RX    |
| GPIO 17   | Viper interface: UART2 RX ← level shifter ← Viper serial TX   |

GPIO 13 is a strapping pin on some ESP32 boards. If SW6 acts weird at boot, move it to another GPIO.

Note: GPIO 16/17 are relay outputs on the relay_controller board and UART2 on the viper_interface board — these are different physical ESP32s, so there is no conflict.

## CAN protocol (11-bit IDs)

| ID     | Name              | Payload                                                |
|--------|-------------------|--------------------------------------------------------|
| 0x100  | RELAY_CMD         | `[mask, state]` — only bits set in mask are applied    |
| 0x101  | RELAY_STATUS      | `[bitmap]` — broadcast at 5 Hz                         |
| 0x200  | SWITCH_EVENT      | `[switch_id, SwitchEvent]`                             |
| 0x201  | ENCODER_EVENT     | `[EncoderEvent, count]`                                |
| 0x300  | TELEMETRY         | `[vbat_cv_lo, vbat_cv_hi, i_da_lo, i_da_hi, vsol_cv_lo, vsol_cv_hi, flags, _]` |
| 0x301  | ENV_DATA          | `[temp_d1_lo, temp_d1_hi, humi_d1_lo, humi_d1_hi]` (0.1°C, 0.1%) |
| 0x302  | IMU_DATA          | `[accel_x_lo, accel_x_hi, accel_y_lo, accel_y_hi, accel_z_lo, accel_z_hi]` |
| 0x303  | SHAKE_EVENT       | `[magnitude, axis_mask]`                               |
| 0x400  | CONFIG_WRITE      | `[target, key, index, kind, arg, arg2_lo, arg2_hi, flags]` |
| 0x401  | CONFIG_READ_REQ   | `[target, key, index]` (index 0xFF = all)              |
| 0x402  | CONFIG_READ_RESP  | same layout as CONFIG_WRITE (flags byte unused)        |
| 0x403  | CONFIG_SAVE       | `[target, action]` — 0x01 commit, 0x02 reload, 0x03 factory reset |
| 0x500  | LCD_CMD           | `[row, col, char…]` or `[0xFF]` clear — any node → switch_panel |
| 0x510  | VIPER_CMD         | `[cmd]` — any node → viper_interface; cmd: 0x01 lock, 0x02 unlock, 0x03 remote start |
| 0x511  | VIPER_STATUS      | `[b0..b4]` — viper_interface → everyone; raw 5-byte Viper alarm packet |

Config targets: `0x01 SWITCH_PANEL`, `0x02 RELAY_CTRL`, `0x03 VIPER`, `0xFF BROADCAST`.

Config keys:
- `0x10 CFG_KEY_SW_ACTION` — per-switch `SwitchAction {kind, arg, arg2}`
- `0x20 CFG_KEY_RELAY_MAX_ON_MS` — per-relay safety auto-off timeout

Switch action kinds:
- `0 SW_ACT_TOGGLE` — press toggles relay `arg`
- `1 SW_ACT_PULSE` — press turns relay `arg` on for `arg2` ms
- `2 SW_ACT_EVENT_ONLY` — publish event only
- `3 SW_ACT_HOLD` — relay `arg` ON while held (horn)
- `4 SW_ACT_SCENE` — press sets all relays to bitmap in `arg`

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

Everything is in the Arduino-ESP32 core; no external libraries required.

- `driver/twai.h` — CAN controller
- `WiFi.h` / `esp_now.h` — wireless
- `WebServer.h` / `DNSServer.h` — HTTP + captive portal
- `Preferences.h` — NVS persistence

Tested against Arduino-ESP32 **v2.x and v3.x**. The ESP-NOW receive callback signature changed in v3.x (`esp_now_recv_info_t*` first arg instead of `const uint8_t* mac`); `bus.cpp` uses a version preprocessor guard to handle both. TWAI APIs also changed in v3.x — if bumping, verify compilation before committing.

## Build & flash (Mac)

Using arduino-cli:

```bash
make relay_controller                              # compile for relay controller
make switch_panel                                  # compile for switch panel
make viper_interface                               # compile for viper interface
make all                                           # compile all three in sequence
make upload-switch_panel PORT=/dev/cu.usbserial-XXXX
make upload-relay_controller PORT=/dev/cu.usbserial-YYYY
make upload-viper_interface PORT=/dev/cu.usbserial-ZZZZ
make monitor PORT=/dev/cu.usbserial-XXXX
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

### Raw hex command line
```
<id_hex> <byte0> <byte1> ...       # up to 8 bytes
```

Examples:
```
100 01 01        # relay 1 on
100 3F 00        # all relays off
401 02 20 FF     # read all relay max-on-ms config
510 01           # viper lock/arm
510 02           # viper unlock/disarm
510 03           # viper remote start
```

### Aliases (shortcut commands, start with `:`)
```
:relay <n> on|off              # n = 1..6
:alloff                        # all 6 off
:horn                          # quick horn pulse
:readcfg sw|relay              # dump node config
:save sw|relay                 # commit RAM config to NVS
:reset sw|relay                # factory reset
:cfgsw <idx> toggle|pulse|event|hold|scene <arg> [arg2] [!]
:cfgrelay <idx> maxon <ms> [!]
:viper lock|unlock|start       # send VIPER_CMD to viper_interface node
```

Trailing `!` persists the change to NVS immediately.

### Wire-failure fallback test
Check "force wifi-only" in the header. `bus_tx()` stops using TWAI; everything should still work via ESP-NOW. Uncheck to restore wire.

## Conventions and gotchas

- **Init order.** In `setup()` the order must be `relay_setup()` → `setup_can()` → `webui_init()` → `bus_init()`. `webui_init` brings up WiFi so `esp_now_init` has a radio to bind to; `bus_init` requires WiFi ready. Relay pins are initialized first so outputs are known-good before any CAN traffic.
- **Frame-flow direction.** Application code only goes through the bus. Do not call `twai_transmit` directly except inside `bus.cpp`; it will bypass ESP-NOW and the web UI log.
- **Self-echo.** `bus_tx()` feeds every outbound frame back into the RX ring (tagged `source="self"`). This means state mirrors and LCD updates react to web-UI-injected frames exactly the same as frames from other nodes. Don't be surprised when you see your own TX come back through `bus_rx()`.
- **Dedup.** ESP-NOW frames carry their own `(node_id, seq)` header; a 2-second window filters repeats. CAN frames are canonical and always passed through. This is safe because our application messages are idempotent (RELAY_CMD, STATUS, TELEMETRY) or edge-triggered with short windows (SWITCH_EVENT).
- **Captive-portal probes.** iOS and Android each hit different URLs to detect captive portals — we catch the common ones in `webui.cpp` and bounce them to `/`.
- **Open AP.** Fine in a garage, risky in public. Set `AP_PASSWORD` in `webui.cpp` before the car leaves the driveway. Remember it must be ≥ 8 chars for WPA2.
- **Horn safety.** `RELAY_MAX_ON_INIT` in `configs/relay_controller.h` caps relay 5 (horn) at 30s. `service_hold_safety()` in `mod_switches.cpp` also enforces that if a HOLD switch is released while the relay is still on, the relay gets turned off.
- **ADC calibration.** GPIO 34 on the relay controller is read with default attenuation; `VBAT_DIVIDER_RATIO` in `configs/relay_controller.h` assumes 10k + 2.2k divider. Re-tune to your resistors before trusting the telemetry.
- **Strapping pins.** Avoid GPIO 0, 2, 12, 15 for anything that's externally driven at reset. GPIO 13 is borderline — watch for flakiness.
- **ULN2803 polarity.** Input high → output low → coil pulled to GND → relay on. `RELAY_ACTIVE_HIGH = true` is correct for ULN2803 + low-side-coil relays. Flip if using high-side-switch drivers.
- **node_config.h is generated.** Never edit `firmware/accessory_node/node_config.h` directly — it gets overwritten by the next `make` invocation. Edit the appropriate `firmware/configs/<node>.h` instead.

## Known TODO list (in rough priority order)

1. Add a password to the SoftAP before any field install (`AP_PASSWORD` in `webui.cpp`).
2. Encrypt ESP-NOW (`esp_now_set_pmk`, mark broadcast peer `encrypt = true`).
3. Dashboard panel in the web UI — live relay states as tiles, switch pressed/released indicators, battery voltage graph. Today the UI is raw-frame-only.
4. Python `can-gw` utility so a Mac with a CANable/MCP2515 USB adapter can be a bus participant for scripting and logging.
5. Low-voltage cutoff in `mod_relay.cpp` — when `vbat_cv` drops below a configurable threshold, force non-essential relays off.
6. Persist the "force wifi-only" flag across reboots (currently RAM-only).
7. Switch from polling to SSE or WebSocket for the frame log (lower latency, less traffic).
8. ESP-NOW ack/retry for switch events specifically (they're the only edge-triggered frames).
9. A third "display" node with a small OLED under the dash showing bus status, for when the car is parked and phone-free.
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

- Switch is pressed on switch_panel → local action dispatch looks up `g_map[idx]` in `mod_switches.cpp` → emits `RELAY_CMD` via `bus_tx()` → bus sends on both CAN and ESP-NOW → relay_controller `bus_rx()` returns the frame → `relay_handle_frame()` calls `apply_relay_cmd()` which flips GPIO → `send_relay_status()` broadcasts new state → switch_panel's `g_relay_mirror` stays in sync → web UI on either node logs every frame in real time.
- Config changes follow the same path: UI or external tool sends `CONFIG_WRITE` → target node's module handler updates RAM, optionally persists to NVS, echoes a `CONFIG_READ_RESP` → sender sees confirmation.
- Viper alarm command: any node (or the web UI) sends `VIPER_CMD (0x510)` with a 1-byte command code → viper_interface `bus_rx()` returns the frame → `viper_handle_frame()` calls the appropriate ViperESP2 method → ViperESP2 writes the 5-byte serial packet to the alarm over UART2 → alarm responds → `on_viper_message()` callback fires → viper_interface calls `bus_tx(VIPER_STATUS)` → all nodes and the web UI log the raw alarm response.
