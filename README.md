# Antique Car CAN Bus Accessory System

A parallel 12V accessory wiring system for an antique car. Three ESP32 nodes talk over a shared CAN bus (with ESP-NOW WiFi fallback), driven by a switch panel, with a browser-based web console on each node and a Viper 5305V car alarm bridged in as a fourth participant.

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Bill of Materials](#2-bill-of-materials)
3. [Node Wiring — Switch Panel](#3-node-wiring--switch-panel)
4. [Node Wiring — Relay Controller](#4-node-wiring--relay-controller)
5. [Node Wiring — Viper Interface](#5-node-wiring--viper-interface)
6. [CAN Bus Backbone](#6-can-bus-backbone)
7. [Power Distribution](#7-power-distribution)
8. [Build and Flash](#8-build-and-flash)
9. [First Boot Checklist](#9-first-boot-checklist)
10. [Web Console](#10-web-console)
11. [LCD Menu System](#11-lcd-menu-system)
12. [Runtime Configuration](#12-runtime-configuration)
13. [CAN Frame Reference](#13-can-frame-reference)
14. [Bench Mode vs Production Mode](#14-bench-mode-vs-production-mode)
15. [Troubleshooting](#15-troubleshooting)

---

## 1. System Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                       ACCESSORY BATTERY / FUSE BOX                 │
└────────────────────────────┬────────────────────────────────────────┘
                             │ 12 V
          ┌──────────────────┼───────────────────┐
          │                  │                   │
 ┌────────┴────────┐ ┌───────┴───────┐  ┌────────┴────────┐
 │  SWITCH PANEL   │ │RELAY CTRL     │  │ VIPER IFACE     │
 │  ESP32 #1       │ │ESP32 #2       │  │ ESP32 #3        │
 │  NODE_ID 0x01   │ │NODE_ID 0x02   │  │ NODE_ID 0x03    │
 │                 │ │               │  │                 │
 │  6 latching sw  │ │  6 relays     │  │  Viper 5305V    │
 │  4 buttons      │ │  (ULN2803)    │  │  serial bridge  │
 │  Rotary encoder │ │  Batt voltage │  │                 │
 │  16×2 LCD       │ │  ADC monitor  │  │                 │
 └────────┬────────┘ └───────┬───────┘  └────────┬────────┘
          │                  │                   │
          └──────────────────┼───────────────────┘
                       CAN BUS (0x510)
                    125 kbit/s, 11-bit IDs
                (+ ESP-NOW on WiFi channel 6 as fallback)
```

All three nodes share the same firmware transport layer (`bus.h/.cpp`), web UI (`webui.h/.cpp`, `index_html.h`), and protocol definitions (`can_protocol.h`). Each node runs its own web console at `http://192.168.4.1` on the `AccessoryBus` WiFi network.

---

## 2. Bill of Materials

### Per Node (× 3)
| Qty | Part | Notes |
|-----|------|-------|
| 1 | ESP32 WROOM-32 dev board | 38-pin variant preferred for GPIO count |
| 1 | USB-to-serial cable | For initial flashing and serial monitor |

### CAN Bus (Production Mode)
| Qty | Part | Notes |
|-----|------|-------|
| 2 | TJA1051T/3 or SN65HVD230 CAN transceiver | One per node on the wire (not needed for viper_interface if it's near the other two) |
| 1 | Twisted pair wire | CANH / CANL, 22–24 AWG |
| 2 | 120 Ω resistor, 0.25 W | One at each physical end of the bus |

### CAN Bus (Bench Mode, short runs only)
| Qty | Part | Notes |
|-----|------|-------|
| 1 | 1 kΩ–4.7 kΩ resistor | Pull-up on shared TX wire to 3V3 |

### Switch Panel Node
| Qty | Part | Notes |
|-----|------|-------|
| 6 | Latching toggle or rocker switch | Normally open, one pole to GPIO, other to GND |
| 4 | Momentary push-button | Same wiring as switches |
| 1 | Rotary encoder | CJMCU-111 (EC11-based); has onboard 3.3 kΩ pull-ups — connect module VCC to 3V3, no external resistors needed. No SW pin (see note below). |
| 1 | Momentary push-button | Encoder select/back for menu system — wire to any free BTN GPIO (e.g. BTN1, GPIO 14) |
| 1 | HD44780-compatible 16×2 LCD with PCF8574 I2C backpack | Default I2C address 0x27; try 0x3F if blank |

### Relay Controller Node
| Qty | Part | Notes |
|-----|------|-------|
| 1 | ULN2803A Darlington array IC | Low-side relay driver, 8 channels (only 6 used) |
| 6 | 12 V relay module (or bare relay + flyback diode) | Coil current ≤ 500 mA per channel on ULN2803 |
| 6 | 10 A automotive fuse + fuse holder | One per relay output |
| 1 | 10 kΩ resistor | Voltage divider — top leg |
| 1 | 2.2 kΩ resistor | Voltage divider — bottom leg (GPIO 34 input) |

### Viper Interface Node
| Qty | Part | Notes |
|-----|------|-------|
| 1 | 3.3 V ↔ 5 V bidirectional level shifter | 2-channel minimum (TX + RX lines) |
| — | Viper 5305V alarm (or compatible) | Existing install |

---

## 3. Node Wiring — Switch Panel

**ESP32 #1 · NODE_ID 0x01**

### 3.1 CAN Bus

| ESP32 Pin | Signal | Goes to |
|-----------|--------|---------|
| GPIO 5 | CAN TX | Transceiver TXD (production) or shared CAN wire (bench) |
| GPIO 4 | CAN RX | Transceiver RXD (production) or shared CAN wire (bench) |
| 3V3 | 3V3 | Transceiver VCC (if using TJA1051T) |
| GND | GND | Transceiver GND |

### 3.2 Switches (SW1–SW6, latching)

All switch inputs use internal INPUT_PULLUP. Wire one terminal to the ESP32 GPIO and the other to GND. Switch closed = LOW = pressed.

| Label | ESP32 Pin | Notes |
|-------|-----------|-------|
| SW1 | GPIO 25 | Default: toggle relay 1 |
| SW2 | GPIO 26 | Default: toggle relay 2 |
| SW3 | GPIO 27 | Default: toggle relay 3 |
| SW4 | GPIO 32 | Default: toggle relay 4 |
| SW5 | GPIO 33 | Default: HOLD relay 5 (horn) while pressed |
| SW6 | GPIO 13 | Default: pulse relay 6 for 3 s. **GPIO 13 is a strapping pin — move to another GPIO if you see boot problems** |

### 3.3 Buttons (BTN1–BTN4, momentary)

Same wiring as switches: one pin to GPIO, other to GND.

| Label | ESP32 Pin | Default Behavior |
|-------|-----------|------------------|
| BTN1 | GPIO 14 | **Menu button** — long-press from idle enters menu; short-press navigates/confirms "< Back"; long-press on action item executes it |
| BTN2 | GPIO 18 | Event only (configure at runtime) |
| BTN3 | GPIO 19 | Event only |
| BTN4 | GPIO 23 | Event only |

> **Special:** Long-pressing SW1 (≥ 600 ms) sends ALL-OFF regardless of its configured action.

### 3.4 Rotary Encoder

Module: **CJMCU-111** (EC11-based). Has onboard 3.3 kΩ pull-up resistors (marked `332`) on GA and GB — no external resistors required. Connect module VCC to the ESP32 3V3 rail.

> **No SW pin:** The CJMCU-111 physically clicks when you press the shaft but the switch contact is not wired to any pin header on the PCB. BTN1 (GPIO 14) serves as the encoder button for the LCD menu system — long-press to enter/back, short-press to select.

```
Encoder GA ──── GPIO 34   (onboard pull-up via module VCC)
Encoder GB ──── GPIO 35   (onboard pull-up via module VCC)
Encoder VCC ─── 3V3
Encoder GND ─── GND
```

| Encoder Pin | ESP32 Pin | Notes |
|-------------|-----------|-------|
| GA (CLK/A) | GPIO 34 | Onboard pull-up — no external resistor needed |
| GB (DT/B) | GPIO 35 | Onboard pull-up — no external resistor needed |
| VCC | 3V3 | Powers the onboard pull-up network |
| GND | GND | — |

### 3.5 LCD (HD44780, PCF8574 I2C Backpack)

| LCD Module Pin | ESP32 Pin | Notes |
|----------------|-----------|-------|
| VCC | 5V (VIN) | Most HD44780 backpacks require 5 V. I2C signals run at 3V3 via the backpack. |
| GND | GND | — |
| SDA | GPIO 21 | I2C data |
| SCL | GPIO 22 | I2C clock |

Default I2C address is `0x27`. If the display stays blank after boot, try `0x3F` (change `LCD_I2C_ADDR` in `switch_panel.ino`).

### 3.6 Full Switch Panel Pin Summary

```
ESP32 #1 (Switch Panel)
─────────────────────────────────────────
3V3  ──── encoder VCC (powers onboard pull-ups)
GND  ──── switch commons, encoder GND
GPIO 4   CAN RX  ←── CAN bus
GPIO 5   CAN TX  ──→ CAN bus
GPIO 13  SW6
GPIO 14  BTN1  (recommended: encoder select/back button for menu)
GPIO 18  BTN2
GPIO 19  BTN3
GPIO 21  LCD SDA
GPIO 22  LCD SCL
GPIO 23  BTN4
GPIO 25  SW1
GPIO 26  SW2
GPIO 27  SW3
GPIO 32  SW4
GPIO 33  SW5
GPIO 34  ENC GA  (CJMCU-111 — no external pull-up needed)
GPIO 35  ENC GB  (CJMCU-111 — no external pull-up needed)
5V (VIN) LCD VCC
```

---

## 4. Node Wiring — Relay Controller

**ESP32 #2 · NODE_ID 0x02**

### 4.1 CAN Bus

Same as switch panel — GPIO 5 = TX, GPIO 4 = RX to the transceiver or shared wire.

### 4.2 Relay Driver (ULN2803A)

The ULN2803A is an 8-channel low-side Darlington driver. ESP32 drives inputs HIGH to energise a relay coil.

```
ESP32 GPIO ──→ ULN2803 Input ──→ (internal NPN) ──→ ULN2803 Output ──→ Relay coil (–)
                                                                        Relay coil (+) ──→ 12 V
                                                  ULN2803 COM ──→ 12 V  (flyback diode rail)
```

| Relay # | ESP32 Pin | ULN2803 Input # | Default Load |
|---------|-----------|-----------------|--------------|
| 1 | GPIO 16 | IN1 | Configurable |
| 2 | GPIO 17 | IN2 | Configurable |
| 3 | GPIO 18 | IN3 | Configurable |
| 4 | GPIO 19 | IN4 | Configurable |
| 5 | GPIO 21 | IN5 | **Horn** (30 s safety cutoff) |
| 6 | GPIO 22 | IN6 | Configurable |

> **ULN2803 COM pin:** Connect to the 12 V rail. The internal flyback diodes clamp inductive spikes from relay coils back to this rail. Do not leave it floating.

### 4.3 Relay Output Connections

Each relay provides Normally Open (NO) and Normally Closed (NC) contacts. For accessory loads, use NO:

```
12 V ──→ Fuse ──→ Relay NO contact ──→ Load ──→ GND
                   Relay COM ──→ 12 V after fuse
```

### 4.4 Battery Voltage Monitor (Optional)

A resistor voltage divider scales 12–15 V down to the 0–3.3 V range that the ESP32 ADC can read.

```
12 V rail ──[10 kΩ]──┬──[2.2 kΩ]── GND
                     └── GPIO 34
```

This gives a divider ratio of 5.545 (10k + 2.2k / 2.2k). Adjust `VBAT_DIVIDER_RATIO` in `relay_controller.ino` to match your actual resistors. Telemetry is reported in centvolts on CAN ID 0x300.

### 4.5 Full Relay Controller Pin Summary

```
ESP32 #2 (Relay Controller)
─────────────────────────────────────────
3V3  ──── (logic supply; ESP32 self-powers from USB or VIN)
GND  ──── ULN2803 GND, relay coil return (through ULN2803)
GPIO 4   CAN RX  ←── CAN bus
GPIO 5   CAN TX  ──→ CAN bus
GPIO 16  Relay 1 ──→ ULN2803 IN1
GPIO 17  Relay 2 ──→ ULN2803 IN2
GPIO 18  Relay 3 ──→ ULN2803 IN3
GPIO 19  Relay 4 ──→ ULN2803 IN4
GPIO 21  Relay 5 ──→ ULN2803 IN5  (horn — 30 s safety cutoff)
GPIO 22  Relay 6 ──→ ULN2803 IN6
GPIO 34  Vbat ADC ←── voltage divider (10 kΩ / 2.2 kΩ)
```

---

## 5. Node Wiring — Viper Interface

**ESP32 #3 · NODE_ID 0x03**

### 5.1 CAN Bus

Same as other nodes — GPIO 5 = TX, GPIO 4 = RX.

### 5.2 Viper 5305V Serial Connection

The Viper alarm communicates at 9600 baud, 8N1, 5 V TTL logic. The ESP32 runs at 3.3 V. Use a bidirectional level shifter on both TX and RX lines.

```
ESP32 3V3 ──→ Level Shifter LV (low-voltage rail)
Viper 5V  ──→ Level Shifter HV (high-voltage rail)

ESP32 GPIO 16 (UART2 TX) ──→ LV1 ─── HV1 ──→ Viper Serial RX
ESP32 GPIO 17 (UART2 RX) ←── LV2 ─── HV2 ←── Viper Serial TX

ESP32 GND ──→ Level Shifter GND
Viper GND ──→ Level Shifter GND  (must share ground)
```

| Signal | ESP32 Pin | Level Shifter Side | Viper Side |
|--------|-----------|--------------------|------------|
| UART TX | GPIO 16 | LV1 | HV1 → Viper RX |
| UART RX | GPIO 17 | LV2 | HV2 ← Viper TX |
| GND | GND | GND | GND |
| — | 3V3 | LV rail | — |
| — | — | HV rail | Viper 5V |

> **Finding the Viper serial pins:** The Viper 5305V has a data/programming port (usually a 4-pin header near the main harness). Refer to your Viper module's documentation for pin 1 (TX), pin 2 (RX), pin 3 (GND), pin 4 (5V). Polarity varies by revision — use `ViperESP2::sniff()` during reverse engineering to confirm byte order before enabling commands.

### 5.3 Full Viper Interface Pin Summary

```
ESP32 #3 (Viper Interface)
─────────────────────────────────────────
3V3  ──── Level shifter LV rail
GND  ──── Level shifter GND, Viper GND (shared)
GPIO 4   CAN RX  ←── CAN bus
GPIO 5   CAN TX  ──→ CAN bus
GPIO 16  UART2 TX ──→ Level shifter LV1 ──→ HV1 ──→ Viper RX
GPIO 17  UART2 RX ←── Level shifter LV2 ←── HV2 ←── Viper TX
```

---

## 6. CAN Bus Backbone

All three nodes connect to the same two-wire CAN bus (CANH / CANL). The bus runs at **125 kbit/s** with standard 11-bit IDs.

### 6.1 Production Mode (Transceivers)

Use a TJA1051T/3 or SN65HVD230 on each node that has a wired bus connection. 120 Ω termination resistors go at the two physical ends of the cable.

```
                        ┌── 120 Ω ──┐
Node A             Node B             Node C
[ESP32]────TX─→[TJA1051]   [TJA1051]←─TX────[ESP32]
[ESP32]←───RX─[TJA1051]──CANH──CANH──[TJA1051]─RX──→[ESP32]
                        └──CANL──CANL──┘
                                              └── 120 Ω ──┘
```

**TJA1051T/3 pinout (SOT23-8 or DIP-8):**

| TJA1051 Pin | Signal | Connect to |
|-------------|--------|------------|
| TXD | ESP32 CAN TX | GPIO 5 |
| RXD | ESP32 CAN RX | GPIO 4 |
| VCC | 3V3 | ESP32 3V3 |
| GND | GND | Common GND |
| CANH | Bus high | Twisted pair CANH |
| CANL | Bus low | Twisted pair CANL |
| STB / EN | Standby / Enable | Tie to GND (always active) |

**SN65HVD230 pinout (SOT23-8):**

| SN65HVD230 Pin | Signal | Connect to |
|----------------|--------|------------|
| D (TXD) | ESP32 CAN TX | GPIO 5 |
| R (RXD) | ESP32 CAN RX | GPIO 4 |
| Vcc | 3.3 V | ESP32 3V3 |
| GND | GND | Common GND |
| CANH | Bus high | Twisted pair CANH |
| CANL | Bus low | Twisted pair CANL |
| RS | Speed / slope | Tie to GND (high-speed mode) |

### 6.2 Bench Mode (No Transceivers)

For testing on a workbench with short (< 1 m) wires, skip the transceivers. GPIO 5 of each node is reconfigured as open-drain via the `GPIO.pin[5].pad_driver` register after `twai_driver_install()`, and TWAI runs in `TWAI_MODE_NO_ACK` to avoid TX queue jams from boot-order differences. This emulates the CAN wired-AND bus topology.

```
All nodes' GPIO 5 (CAN TX) ──┬──── [1 kΩ – 4.7 kΩ pull-up] ──── 3V3
                             └──── All nodes' GPIO 4 (CAN RX)
```

> **Both nodes must have `#define USE_CAN_TRANSCEIVER 0`** (or both `1`). A mismatch will corrupt the bus.

---

## 7. Power Distribution

The system runs off a **dedicated accessory battery** isolated from the factory wiring.

```
[Accessory Battery +12V]
        │
   [Main Fuse]
        │
   [Fuse Block]
   ├── F1 ── Relay Controller (12V logic via 7805 or buck converter → ESP32 VIN)
   ├── F2 ── Switch Panel ESP32 VIN
   ├── F3 ── Viper Interface ESP32 VIN
   ├── F4 ── Relay 1 load circuit
   ├── F5 ── Relay 2 load circuit
   ├── F6 ── Relay 3 load circuit
   ├── F7 ── Relay 4 load circuit
   ├── F8 ── Relay 5 load circuit (horn)
   └── F9 ── Relay 6 load circuit

[Accessory Battery GND] ──── Common chassis GND
```

- ESP32 dev boards accept 5–12 V on their VIN pin (or use a small 12V → 5V buck converter).
- All ground returns must share a common point — star ground at the battery negative terminal is preferred.

---

## 8. Build and Flash

### 8.1 Prerequisites

- [Arduino IDE 2.x](https://www.arduino.cc/en/software) or [arduino-cli](https://arduino.github.io/arduino-cli/)
- ESP32 board package: **arduino-esp32 v2.x** (v3.x changed TWAI APIs — verify compilation if upgrading)
- No additional libraries needed — everything uses the ESP32 Arduino core

Install arduino-esp32 in Arduino IDE: **File → Preferences → Additional boards manager URLs** → add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`. Then **Tools → Board → Boards Manager**, search "esp32", install.

### 8.2 Sync Shared Files First

The six shared files live in `firmware/shared/`. Before compiling, sync them to each sketch folder. The easiest way is via the `Makefile`:

```bash
make sync       # copy shared/ → each sketch folder
make all        # sync + compile all three nodes
make check      # verify all copies match shared/
```

Or manually:

```bash
for f in can_protocol.h bus.h bus.cpp webui.h webui.cpp index_html.h; do
  cp firmware/relay_controller/$f firmware/switch_panel/$f
  cp firmware/relay_controller/$f firmware/viper_interface/$f
done
```

### 8.3 Configure Each Sketch

In each `.ino` file, set the transceiver mode before compiling:

```cpp
#define USE_CAN_TRANSCEIVER 0   // bench mode — open-drain TX, pull-up required
// or
#define USE_CAN_TRANSCEIVER 1   // production mode — transceivers on GPIO 5/4
```

All three nodes must agree on this setting.

### 8.4 Compile and Upload

**arduino-cli:**

```bash
# Find your serial ports
ls /dev/cu.usbserial-* /dev/cu.wchusbserial-* /dev/cu.SLAB_USBtoUART 2>/dev/null

# Compile all three
for n in relay_controller switch_panel viper_interface; do
  arduino-cli compile --fqbn esp32:esp32:esp32 firmware/$n || break
done

# Upload (substitute your actual port paths)
arduino-cli upload -p /dev/cu.usbserial-XXXX --fqbn esp32:esp32:esp32 firmware/relay_controller
arduino-cli upload -p /dev/cu.usbserial-YYYY --fqbn esp32:esp32:esp32 firmware/switch_panel
arduino-cli upload -p /dev/cu.usbserial-ZZZZ --fqbn esp32:esp32:esp32 firmware/viper_interface
```

**Arduino IDE:**
Open each `.ino` in the IDE, select **Tools → Board → ESP32 Dev Module**, choose the correct port, and click Upload.

### 8.5 Monitor All Three Nodes Simultaneously

```bash
# Requires tmux (brew install tmux)
tmux new-session \; \
  send-keys 'arduino-cli monitor -p /dev/cu.usbserial-XXXX -c baudrate=115200' C-m \; \
  split-window -h \; send-keys 'arduino-cli monitor -p /dev/cu.usbserial-YYYY -c baudrate=115200' C-m \; \
  split-window -v \; send-keys 'arduino-cli monitor -p /dev/cu.usbserial-ZZZZ -c baudrate=115200' C-m
```

---

## 9. First Boot Checklist

1. **All three nodes** show `=== ... boot ===` on serial at 115200 baud.
2. `[CAN] BENCH mode` or `[CAN] TRANSCEIVER mode` matches your wiring.
3. `[boot] N inputs ready` on the switch panel.
4. `[boot] 6 relays ready (all OFF)` on the relay controller.
5. `[boot] viper interface ready` on the viper interface.
6. `[LCD] init OK` on the switch panel (if LCD wired).
7. WiFi network **AccessoryBus** appears on your phone within ~5 seconds.
8. Connecting to **AccessoryBus** opens the captive portal (or browse to `http://192.168.4.1`).
9. Press SW1 — relay 1 should click on; press again to turn off. The web console logs both the `RELAY_CMD` TX and the `RELAY_STATUS` RX.
10. `:viper lock` from the web console should cause the alarm to arm (Viper status LED blinks / chirps).

---

## 10. Web Console

Each node runs an HTTP server with a captive DNS portal on its `AccessoryBus` AP. Any device on the same WiFi will reach the same bus — frames are shared across nodes via ESP-NOW.

**Access:** Connect to WiFi SSID `AccessoryBus` → browser auto-opens, or navigate to `http://192.168.4.1`.

### 10.1 Raw CAN Frame Entry

```
<id_hex> <byte0> <byte1> ...    # up to 8 bytes, hex, space-separated
```

Examples:
```
100 01 01       relay 1 ON
100 01 00       relay 1 OFF
100 3F 00       all relays OFF
510 01          Viper lock / arm
510 02          Viper unlock / disarm
510 03          Viper remote start
```

### 10.2 Alias Commands

All aliases start with `:`.

| Command | Action |
|---------|--------|
| `:relay <n> on\|off` | Turn relay n (1–6) on or off |
| `:alloff` | Turn all 6 relays off |
| `:horn` | Turn relay 5 (horn) on — subject to 30 s watchdog |
| `:viper lock` | Send VIPER_CMD lock to the Viper interface node |
| `:viper unlock` | Send VIPER_CMD unlock |
| `:viper start` | Send VIPER_CMD remote start |
| `:readcfg sw` | Dump all switch-panel input mappings |
| `:readcfg relay` | Dump all relay max-on-ms settings |
| `:save sw` | Persist switch-panel config to NVS |
| `:save relay` | Persist relay config to NVS |
| `:reset sw` | Factory-reset switch-panel config |
| `:reset relay` | Factory-reset relay config |
| `:cfgsw <idx> toggle\|pulse\|event\|hold\|scene <arg> [arg2] [!]` | Reconfigure input idx |
| `:cfgrelay <idx> maxon <ms> [!]` | Set per-relay safety auto-off (0 = no limit) |

Trailing `!` on `:cfgsw` and `:cfgrelay` persists the change to NVS immediately.

### 10.3 Force WiFi-Only Mode

The checkbox in the page header cuts wired CAN TX. Useful to test that the ESP-NOW fallback path works. Uncheck to restore the wired bus. (This setting is RAM-only and resets on power cycle.)

---

## 11. LCD Menu System

The switch panel has a built-in menu driven by the rotary encoder and BTN1 (GPIO 14).

### Navigation

| Input | Action |
|-------|--------|
| Long-press BTN1 from idle (≥ 600 ms) | Enter menu |
| Short-press BTN1 in menu | Navigate: enter submenu, or confirm "< Back" / "< Exit" |
| Long-press BTN1 on an action item | Execute / toggle (relay, Viper command, backlight) |
| Rotate encoder CW / CCW | Scroll down / up |
| 15 s no input | Auto-exit back to status display |

### Menu Structure

Every list starts with **"< Back"** (or **"< Exit"** at the top level). Scroll to it and short-press to go up a level. Action items require a **long-press** to execute.

```
[idle: CAN status + last event]
│
└─ MENU  (long-press BTN1)
   ├─ < Exit           short press → exit menu
   ├─ Relays
   │   ├─ < Back       short press → return to MENU
   │   └─ Relay 1–6    long press → toggle ON/OFF
   ├─ Viper
   │   ├─ < Back
   │   ├─ Lock / Arm       long press → send command
   │   ├─ Unlock/Disarm    long press → send command
   │   └─ Remote Start     long press → send command
   ├─ Bus Status       live TWAI error counters (read-only)
   │   └─ < Back
   └─ Display
       ├─ < Back
       └─ Backlight    long press → toggle ON/OFF
```

### LCD Layout

**Idle (auto-display):**
```
CAN:OK  [1-3---]     ← row 0: bus state + relay bitmap (1–6 = ON, - = OFF)
Relay 1 ON           ← row 1: last meaningful event
```

**Navigating top-level menu:**
```
MENU  (2/5)
> Relays
```

**Inside Relays submenu, relay selected:**
```
Relays (2/6)
> Relay 2  [OFF]     ← long-press BTN1 to toggle
```

**"< Back" selected:**
```
Relays
< Back               ← short-press BTN1 to go back
```

---

## 12. Runtime Configuration

Switch-to-relay mappings and per-relay safety timeouts are stored in NVS (non-volatile flash) and can be changed without reflashing.

### 11.1 Reconfigure a Switch

```
:cfgsw <index> <action> <arg> [arg2] [!]
```

| Index | Physical Input |
|-------|----------------|
| 0–5 | SW1–SW6 (latching switches) |
| 6–9 | BTN1–BTN4 (momentary buttons) |

| Action | arg | arg2 | Behaviour |
|--------|-----|------|-----------|
| `toggle` | relay idx (0–5) | — | Press toggles relay |
| `pulse` | relay idx | duration ms | Press turns relay on for N ms |
| `hold` | relay idx | — | Relay on while held, off on release |
| `scene` | 6-bit bitmap | — | Press sets all 6 relays to bitmap |
| `event` | — | — | Sends SWITCH_EVENT only, no relay change |

Examples:
```
:cfgsw 0 toggle 0          SW1 toggles relay 1
:cfgsw 5 pulse 5 500       SW6 pulses relay 6 for 500 ms
:cfgsw 4 hold 4            SW5 holds relay 5 (horn)
:cfgsw 6 scene 0x15        BTN1 sets relays 1,3,5 on, others off
:cfgsw 0 toggle 0 !        Same as first, and immediately save to NVS
```

### 11.2 Set Relay Safety Timeout

```
:cfgrelay <relay_index> maxon <milliseconds> [!]
```

Relay index 0–5 maps to relays 1–6. `0` means no limit.

```
:cfgrelay 4 maxon 5000     relay 5 (horn) cuts off after 5 s
:cfgrelay 4 maxon 0 !      remove the limit and save
```

### 11.3 Persist Changes

Changes sent via `:cfgsw` or `:cfgrelay` without `!` are RAM-only and lost on reboot. Save manually with:

```
:save sw       save all switch mappings to NVS
:save relay    save all relay timeouts to NVS
```

---

## 13. CAN Frame Reference

| ID | Name | Direction | Payload |
|----|------|-----------|---------|
| 0x100 | RELAY_CMD | Any → relay_controller | `[mask, state]` — bit N = relay N+1; only mask bits change |
| 0x101 | RELAY_STATUS | relay_controller → all | `[bitmap]` — bit N = relay N+1 on/off; 5 Hz |
| 0x200 | SWITCH_EVENT | switch_panel → all | `[input_id, event]` — event: 0 release, 1 press, 2 long, 3 double |
| 0x201 | ENCODER_EVENT | switch_panel → all | `[event, count]` — event: 0 CW, 1 CCW, 2 press, 3 release, 4 long |
| 0x300 | TELEMETRY | relay_controller → all | `[vbat_cv_lo, vbat_cv_hi, …]` — battery in centvolts; 1 Hz |
| 0x400 | CONFIG_WRITE | any → target node | `[target, key, idx, kind, arg, arg2_lo, arg2_hi, flags]` |
| 0x401 | CONFIG_READ_REQ | any → target node | `[target, key, idx]` — idx 0xFF = all |
| 0x402 | CONFIG_READ_RESP | target → sender | same layout as CONFIG_WRITE |
| 0x403 | CONFIG_SAVE | any → target node | `[target, action]` — 0x01 commit, 0x02 reload, 0x03 factory reset |
| 0x500 | LCD_CMD | any → switch_panel | `[row, col, char…]` or `[0xFF]` clear |
| 0x510 | VIPER_CMD | any → viper_interface | `[cmd]` — 0x01 lock, 0x02 unlock, 0x03 remote start |
| 0x511 | VIPER_STATUS | viper_interface → all | `[b0..b4]` — raw 5-byte Viper alarm response packet |

**Config targets:** `0x01` switch_panel · `0x02` relay_controller · `0x03` viper_interface · `0xFF` broadcast

**Config keys:** `0x10` CFG_KEY_SW_ACTION · `0x20` CFG_KEY_RELAY_MAX_ON_MS

---

## 14. Bench Mode vs Production Mode

| | Bench Mode | Production Mode |
|---|---|---|
| `USE_CAN_TRANSCEIVER` | `0` | `1` |
| Transceivers | Not needed | TJA1051T/3 or SN65HVD230 on each node |
| GPIO 5 | Reconfigured to open-drain via `pad_driver` register | Normal push-pull output to transceiver TXD |
| Physical wiring | All GPIO 5 + GPIO 4 on a single shared wire + pull-up | Twisted pair CANH/CANL, 120 Ω at each end |
| Max cable length | ~1 m | ~40 m at 125 kbit/s |
| Termination | 1–4.7 kΩ pull-up to 3V3 | 120 Ω at each physical end of the bus |

Both nodes must always match. A mismatch between `USE_CAN_TRANSCEIVER = 0` and `= 1` on different nodes will cause bus errors.

---

## 15. Troubleshooting

### CAN Bus Not Working

- Check that `USE_CAN_TRANSCEIVER` is the same value on all nodes.
- In bench mode: confirm the shared pull-up resistor is present on the wire.
- In production mode: confirm 120 Ω termination at each end; check transceiver VCC and GND.
- Serial monitor will show `[CAN] init failed` if `twai_driver_install` returns an error — usually a GPIO conflict.
- The web UI header shows `CAN: OK` / `CAN: LOST` based on RX activity in the last 5 seconds.

### Relay Doesn't Fire

- Check `RELAY_ACTIVE_HIGH = true` — correct for ULN2803 (low-side switching). Flip if using a high-side driver.
- Confirm ULN2803 COM pin is connected to 12 V.
- Confirm GPIO 16–22 on the relay_controller board are not being used for something else.

### Relay Cuts Off Unexpectedly

- A per-relay `max_on_ms` safety timer is in effect. Default for relay 5 (horn) is 30,000 ms. Check with `:readcfg relay` and adjust with `:cfgrelay`.

### LCD Stays Blank

- Try I2C address `0x3F` instead of `0x27` (change `LCD_I2C_ADDR` in `switch_panel.ino`).
- Confirm LCD VCC is on 5 V, not 3.3 V.
- Use an I2C scanner sketch to confirm the backpack is visible on the bus.

### Encoder Behaves Erratically

- GPIOs 34/35 are input-only with no internal pull-up, but the CJMCU-111 has onboard pull-ups — confirm the module VCC pin is connected to 3V3. If VCC is floating the signals will be noisy.
- If the encoder skips detents or double-counts, the gray-code state machine handles signal bounce; erratic behaviour usually means VCC is missing or a wire is intermittent.

### Viper Commands Have No Effect

- Confirm the level shifter is bidirectional and powered (LV = 3V3, HV = 5V, shared GND).
- Run `ViperESP2::sniff()` in the main loop (temporarily, instead of `update()`) and check the serial monitor for raw bytes from the alarm — confirms RX is alive.
- Verify TX and RX are not swapped at the Viper connector.
- Check `[cmd via can]` or `[cmd via wifi]` appears in the relay_controller serial log when you send `:viper lock` — this confirms the CAN frame reached the viper_interface node.

### WiFi / ESP-NOW Fallback Not Working

- Confirm all nodes use the same WiFi channel (hardcoded to channel 6 in `webui.cpp`).
- The frame log will show `[via wifi]` on received frames when the wired bus is cut and ESP-NOW is carrying traffic.
- Check "force wifi-only" in the web UI header to simulate a wire failure without disconnecting anything.

### GPIO 13 (SW6) Misbehaves at Boot

GPIO 13 is a strapping pin on some ESP32 modules. If SW6 triggers spurious events during power-on, move it to another GPIO and update `INPUT_PINS` in `switch_panel.ino`.
