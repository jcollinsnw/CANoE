# CANoE — CAN over ESP

<p align="center">
  <img src="assets/canoe.png" alt="CANoE logo" width="420"/>
</p>

**Paddle your own canoe** — a maker-friendly, off-the-shelf microcontroller platform for wiring up anything with a 12V battery and opinions about fuel. Five ESP32 nodes ride a shared CAN bus, with ESP-NOW as extra sensory perception when the wire gives up. Out of the box it handles a switch panel, a 6-relay fuse box, a Viper 5305V alarm bridge, a dual-mode fuel controller, and a CAN-to-WiFi bridge that ties the whole bus into your home network.

Each node hosts its own browser-based web console for tuning AFR targets, editing rules, or watching raw CAN frames scroll by. The whole thing builds from one unified Arduino sketch — just pick a config header and flash.

All nodes compile from the same unified sketch (`firmware/accessory_node/`). Features are controlled entirely by a per-node config header in `firmware/configs/`.

---

## Nodes

| Node | ID | Config | Key features |
|------|----|--------|-------------|
| switch_panel | 0x01 | `configs/switch_panel.h` | Switches, buttons, encoder, LCD, menu, buzzer, LEDs, rules |
| relay_controller | 0x02 | `configs/relay_controller.h` | 6 relays, battery ADC, RPM sensor |
| viper_interface | 0x03 | `configs/viper_interface.h` | Viper 5305V serial bridge, LCD, MPU-6050 |
| ecu_node | 0x04 | `configs/ecu_node.h` | MAP/TPS/CLT/IAT sensors, WBO2, RPM, carb/injection fuel control |
| bridge | 0x05 | `configs/bridge.h` | CAN ↔ home WiFi, MQTT publish, node discovery web UI |

---

## Documentation

| Guide | Contents |
|-------|----------|
| [Wiring](docs/wiring.md) | Bill of materials, pinouts for all nodes, CAN bus backbone, power distribution |
| [Build & Flash](docs/build.md) | Prerequisites, compiling, uploading, first-boot checklist |
| [Web Console](docs/web-console.md) | Browser UI, command line, tabs, alias commands |
| [LCD Menu](docs/lcd-menu.md) | Menu navigation, layout, relay labels and custom icons |
| [CAN Bus Reference](docs/can-bus-reference.md) | Frame table, config protocol, rules engine, runtime examples |
| [Troubleshooting](docs/troubleshooting.md) | Common problems and fixes for all nodes |
| [New Node Guide](docs/new-node-guide.md) | Adding a new ESP32 node from scratch |

---

## Firmware Modules

Each module lives in `firmware/accessory_node/mod_*.cpp` and is compiled in only when its `ENABLE_*` flag is defined in the node config.

| Module | Flag | Description |
|--------|------|-------------|
| [bus](docs/modules/bus.md) | *(always)* | Dual-transport abstraction — TWAI + ESP-NOW |
| [webui](docs/modules/webui.md) | *(always)* | SoftAP, captive portal, HTTP + JSON API |
| [relay](docs/modules/relay.md) | `ENABLE_RELAY` | 6 relay GPIO outputs, safety watchdog, battery ADC |
| [switches](docs/modules/switches.md) | `ENABLE_SWITCHES` | Switch/button/encoder inputs → SWITCH_EVENT / ENCODER_EVENT |
| [rules](docs/modules/rules.md) | `ENABLE_RULES` | CAN-frame-triggered rules engine, NVS-persisted |
| [lcd](docs/modules/lcd.md) | `ENABLE_LCD` | HD44780 16×2 driver, widget system, CGRAM icons |
| [menu](docs/modules/menu.md) | `ENABLE_MENU` | Encoder-driven LCD menu (requires LCD) |
| [buzzer](docs/modules/buzzer.md) | `ENABLE_BUZZER` | Passive piezo tone sequencer |
| [led](docs/modules/led.md) | `ENABLE_LEDS` | CAN-addressable status LEDs |
| [viper](docs/modules/viper.md) | `ENABLE_VIPER` | Viper 5305V serial bridge over UART2 |
| [mpu6050](docs/modules/mpu6050.md) | `ENABLE_MPU6050` | MPU-6050 accelerometer / shake detection |
| [dht22](docs/modules/dht22.md) | `ENABLE_DHT22` | AM2302 temperature/humidity sensor |
| [rpm](docs/modules/rpm.md) | `ENABLE_RPM` | Engine RPM via optocoupler interrupt |
| [gps](docs/modules/gps.md) | `ENABLE_GPS` | GPS speed/heading via NMEA UART |
| [wbo2](docs/modules/wbo2.md) | `ENABLE_WBO2` | Wideband O2 sensor analog read |
| [ecu](docs/modules/ecu.md) | `ENABLE_ECU` | Dual-mode fuel controller (carb PI + TBI injection) |
| [bluetooth](docs/modules/bluetooth.md) | `ENABLE_BLUETOOTH` | BLE GATT CAN mirror — native iOS app + auto GPS injection |
| [mqtt](docs/modules/mqtt.md) | `MQTT_BROKER` | MQTT bridge: publish all frames, inject via subscribe |
| [serial_shell](docs/modules/serial_shell.md) | *(always)* | Serial debug shell |
| blob | *(always)* | Generic chunked blob write protocol (BLOB_WRITE 0x410 / BLOB_COMMIT 0x411) |
| wifi_creds | *(always)* | Runtime WiFi + ESP-NOW credential storage (NVS `"wifi_creds"`) |

---

## Tuner iOS App

[`tuner/`](tuner/) is a native Swift app that pairs with any node running `ENABLE_BLUETOOTH`.

- **BLE auto-pair** — saves the peripheral UUID on first connect; reconnects automatically on every ignition cycle without user interaction, even with the app backgrounded.
- **Live CAN frame log** — dark monospaced display styled after the web console, showing decoded IDs and payloads in real time.
- **Native GPS injection** — CoreLocation speed and heading encoded as `GPS_DATA (0x305)` frames, sent over BLE while driving. Keeps running with the screen off.
- **Web console access** — "Open Web Console" sheet loads the full in-node web UI (rules editor, relay controls, etc.) from inside the app.
- **Wi-Fi fallback** — connect by IP to open the web UI directly without BLE.

See the [Tuner README](tuner/README.md) for build instructions and GATT profile details.
