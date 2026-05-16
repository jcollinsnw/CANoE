# CANoE — Build & Flash Guide

Compiling firmware, flashing nodes, and verifying a successful first boot.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Configure the Target Node](#2-configure-the-target-node)
3. [Compile and Upload (USB)](#3-compile-and-upload-usb)
4. [OTA Updates (WiFi Flash)](#4-ota-updates-wifi-flash)
5. [Monitor Serial Output](#5-monitor-serial-output)
6. [First Boot Checklist](#6-first-boot-checklist)

---

## 1. Prerequisites

- [arduino-cli](https://arduino.github.io/arduino-cli/) (recommended) or Arduino IDE 2.x
- ESP32 board package: **arduino-esp32 v2.x or v3.x**

**arduino-cli setup:**
```bash
arduino-cli core install esp32:esp32
```

**M5Stack Cardputer (ESP32-S3) only:** Add the M5Stack board manager URL and install:
```bash
arduino-cli config add board_manager.additional_urls https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
arduino-cli core install m5stack:esp32
```

**Arduino IDE setup:** File → Preferences → Additional boards manager URLs → add:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Then Tools → Board → Boards Manager → search "esp32" → install. For Cardputer support, also add the M5Stack URL above and install "M5Stack".

**MQTT bridge only:** Install the PubSubClient library if flashing the bridge node with `MQTT_BROKER` defined:
```bash
arduino-cli lib install "PubSubClient"
```

---

## 2. Configure the Target Node

Each node has a config header in `firmware/configs/`. Open the appropriate file and set the transceiver mode to match your wiring:

```cpp
#define USE_CAN_TRANSCEIVER 0   // bench mode — open-drain TX, pull-up required
// or
#define USE_CAN_TRANSCEIVER 1   // production mode — TJA1051/SN65HVD230 transceivers
```

All nodes on the same physical bus must agree on this setting.

For the bridge node, also set your home WiFi credentials in `firmware/configs/bridge.h`:
```cpp
#define STA_SSID     "YourHomeNetwork"
#define STA_PASSWORD "YourPassword"
```

> `node_config.h` is a generated file — never edit it directly. It gets overwritten by every `make` invocation. Always edit `firmware/configs/<node>.h`.

---

## 3. Compile and Upload (USB)

The Makefile copies the right config header and compiles the single unified sketch. Find your serial ports first:

```bash
ls /dev/cu.usbserial-* /dev/cu.wchusbserial-* /dev/cu.SLAB_USBtoUART 2>/dev/null
```

**Compile all nodes:**
```bash
make all
```

**Compile and upload individual nodes:**
```bash
make upload-relay   PORT=/dev/cu.usbserial-XXXX
make upload-switch  PORT=/dev/cu.usbserial-YYYY
make upload-viper   PORT=/dev/cu.usbserial-ZZZZ
make upload-ecu     PORT=/dev/cu.usbserial-WWWW
make upload-bridge  PORT=/dev/cu.usbserial-VVVV
make upload-cardputer                              # copies to /Volumes/CARDPUTER/CANoE/ then unmounts
```

**Compile only (no upload):**
```bash
make relay
make switch
make viper
make ecu
make bridge
make cardputer
```

Compiled binaries land in `build/<node>/accessory_node.ino.bin` for use with OTA or the browser upload. The Cardputer binary is also copied to `build/cardputer/m5canoe.bin`. The Cardputer target uses the M5Stack FQBN (`m5stack:esp32:m5stack_cardputer`) and does not produce a web UI — it has no SoftAP.

**Cardputer upload (UF2 / mass storage):** `make upload-cardputer` copies `m5canoe.bin` to the Cardputer's mass-storage volume and unmounts it. The Cardputer must be in UF2 mode first — hold the G0 key while plugging in USB until it mounts as a drive. Volume path and subdirectory are configurable:
```bash
make upload-cardputer                                              # default: /Volumes/CARDPUTER/CANoE/
make upload-cardputer CARDPUTER_VOLUME=/Volumes/MYCARD CARDPUTER_DIR=firmware
```

**Arduino IDE (manual):** Copy the config before opening the sketch:
```bash
cp firmware/configs/switch_panel.h firmware/accessory_node/node_config.h
```
Then open `firmware/accessory_node/accessory_node.ino`, select Tools → Board → ESP32 Dev Module, choose the port, and click Upload. Repeat for each node.

---

## 4. OTA Updates (WiFi Flash)

After the first USB flash, all subsequent updates can be done wirelessly over the node's SoftAP — no cable required.

### One-time USB requirement

The firmware uses the `min_spiffs` partition scheme, which reserves two app slots for OTA. The partition table is written during the **first USB flash**. Every node must be flashed at least once via USB before OTA will work. After that first flash, USB is optional.

### From the terminal

Connect your Mac to the node's AP (`AccessoryBus`), then run the matching `ota-*` target. It recompiles and uploads in one step:

```bash
make ota-relay
make ota-switch
make ota-viper
make ota-ecu
make ota-bridge
```

`OTA_IP` defaults to `192.168.4.1`. Override it if the node is reachable at a different address (e.g. the bridge node via your home network):

```bash
make ota-bridge OTA_IP=192.168.1.42
```

### From the browser

Connect to the node's AP, open `http://192.168.4.1`, go to **Settings → Firmware Update**. Select `build/<node>/accessory_node.ino.bin` and click **Flash Firmware**. A progress bar tracks the upload; the node reboots automatically when done.

> **Select the correct binary for the node you are connected to.** The Settings panel shows the current node name as a reminder. Flashing the wrong binary (e.g. `relay_controller` firmware onto the `switch_panel`) produces a live but misconfigured node — fix it with another OTA or USB flash of the correct binary.

---

## 5. Monitor Serial Output

All nodes log to serial at **115200 baud**.

**Single node:**
```bash
make monitor PORT=/dev/cu.usbserial-XXXX
# or directly:
arduino-cli monitor -p /dev/cu.usbserial-XXXX -c baudrate=115200
```

**All nodes simultaneously (requires tmux):**
```bash
tmux new-session \; \
  send-keys 'arduino-cli monitor -p /dev/cu.usbserial-XXXX -c baudrate=115200' C-m \; \
  split-window -h \; send-keys 'arduino-cli monitor -p /dev/cu.usbserial-YYYY -c baudrate=115200' C-m \; \
  split-window -v \; send-keys 'arduino-cli monitor -p /dev/cu.usbserial-ZZZZ -c baudrate=115200' C-m
```

Nodes with WiFi also expose serial output in the web UI's **Serial** tab via `wlog()`/`wlogln()`.

---

## 6. First Boot Checklist

Work through this after flashing all nodes for the first time.

1. **Boot message** — each node prints `=== <node-name> boot ===` at 115200 baud.

2. **CAN mode** — serial shows `[CAN] BENCH mode` or `[CAN] TRANSCEIVER mode`. Confirm it matches the wiring.

3. **Node-specific init:**
   - Switch panel: `[boot] N inputs ready`
   - Relay controller: `[boot] 6 relays ready (all OFF)`
   - Viper interface: `[boot] viper interface ready`
   - ECU node: `[ecu] base_pw=XXXX us  disp/cyl=XXX cc  n_inj=N`
   - Bridge node: `[wifi] STA connected, IP=192.168.x.x`
   - Cardputer: `[boot] ESP-NOW only mode` (TFT shows relay status bar + CLI prompt)

4. **LCD** — switch panel prints `[LCD] init OK`. If the display stays blank, try the alternate I2C address (`LCD_I2C_ADDR 0x3F`).

5. **WiFi** — the network **AccessoryBus** appears on nearby devices within ~5 seconds of boot.

6. **Web console** — connect to **AccessoryBus**, navigate to `http://192.168.4.1`. The frame log and node status should be visible.

7. **Basic relay test** — press SW1. The frame log should show `SWITCH_EVENT → RELAY_CMD → RELAY_STATUS` in sequence, and relay 1 should click on.

8. **Viper test** — type `:viper lock` in the web console. The alarm should arm (LED blinks or chirps).

9. **Bridge test** — if using the bridge node, connect a device on your home network and browse to the bridge's assigned IP. The aggregated control panel should show all other nodes.

10. **Security** — before the car leaves the driveway, set `AP_PASSWORD` (≥ 8 chars, must match on all WiFi nodes) in each node's config header and reflash. Open AP is fine in a closed garage, not in public.
