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
12. [Relay Customization (Labels and Icons)](#12-relay-customization-labels-and-icons)
13. [Runtime Configuration](#13-runtime-configuration)
14. [CAN Frame Reference](#14-can-frame-reference)
15. [Bench Mode vs Production Mode](#15-bench-mode-vs-production-mode)
16. [Troubleshooting](#16-troubleshooting)

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

All three nodes are compiled from the same unified sketch (`firmware/accessory_node/`). Which features compile in — relays, switches, LCD, Viper bridge, etc. — is controlled entirely by a per-node config header in `firmware/configs/`. Each node runs its own web console at `http://192.168.4.1` on the `AccessoryBus` WiFi network.

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
| 1 | Passive piezo buzzer | 3.3 V-compatible; positive leg to GPIO 16 (optionally via 100Ω series resistor), negative to GND |
| 1 | 100 Ω resistor, ¼ W *(optional)* | Series resistor on buzzer positive leg to reduce volume |
| 3 | LED (any colour, 3 mm or 5 mm) | Status LEDs on GPIO 17, 19, 23; controlled over CAN |
| 3 | 330 Ω resistor, ¼ W | Series resistor for each LED (adjust for desired brightness) |
| 2 | 10 kΩ resistor, ¼ W | Pull-up to 3V3 for BTN3 (GPIO 36) and BTN4 (GPIO 39) — these pins have no internal pull-up |

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

| Label | ESP32 Pin | Default Rule |
|-------|-----------|--------------|
| SW1 | GPIO 25 | Press → toggle relay 1 |
| SW2 | GPIO 26 | Press → toggle relay 2 |
| SW3 | GPIO 27 | Press → toggle relay 3 |
| SW4 | GPIO 32 | Press → toggle relay 4 |
| SW5 | GPIO 33 | Press → relay 5 ON; Release → relay 5 OFF (HOLD / horn) |
| SW6 | GPIO 13 | Configurable. **GPIO 13 is a strapping pin — move to another GPIO if you see boot problems** |

### 3.3 Buttons (BTN1–BTN4, momentary)

Same wiring as switches: one pin to GPIO, other to GND.

| Label | ESP32 Pin | Default Rule |
|-------|-----------|--------------|
| BTN1 | GPIO 14 | Long-press → enter menu; short-press in menu → select |
| BTN2 | GPIO 18 | Press → all 6 relays off |
| BTN3 | GPIO 36 | Configurable (EVENT_ONLY by default) |
| BTN4 | GPIO 39 | Configurable (EVENT_ONLY by default) |

All input-to-action mappings are driven by the **rules engine** (`RULES_DEFAULT_INIT` in `switch_panel.h`) and fully reassignable at runtime via the web console Rules tab.

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

### 3.6 Piezo Buzzer

A passive piezo buzzer provides audio feedback for menu interactions and relay state changes. It must be a **passive** (not active) buzzer — the firmware generates the frequencies itself using Arduino `tone()`.

```
GPIO 16 ──[100Ω]──┬── Buzzer (+)
                  │
                 (optional resistor, reduces volume)

GND ──────────────── Buzzer (–)
```

| Buzzer Pin | ESP32 Pin | Notes |
|------------|-----------|-------|
| + (positive) | GPIO 16 | Via optional 100Ω series resistor |
| – (negative) | GND | — |

> **Active vs passive:** An active buzzer has a built-in oscillator and only needs power — it will just beep at one fixed pitch. A passive piezo has no oscillator; the ESP32 drives it at specific frequencies. The firmware requires a passive piezo to produce distinct tones.

**Sound events:**

| Event | Sound |
|-------|-------|
| Menu enter | Two-tone rising (E5 → C6) |
| Menu exit | Two-tone falling (C6 → E5) |
| Menu scroll | Short tick (G5, 18 ms) |
| Enter submenu | Rising two-tone (G5 → C6) |
| Execute action | Three-note rising flourish |
| Relay ON | Rising chirp (G5 → C6) |
| Relay OFF | Falling chirp (C6 → E5) |
| All OFF | Descending three-note sweep |

Relay sounds trigger on any RELAY_CMD frame — whether from a physical switch, the LCD menu, or the web console.

### 3.7 Status LEDs

Three LEDs controlled via CAN frame 0x102 (LED_CMD). Any node on the bus can address them.

```
GPIO 17 ──[330Ω]──── LED 1 anode
GPIO 19 ──[330Ω]──── LED 2 anode     LED cathodes ──── GND
GPIO 23 ──[330Ω]──── LED 3 anode
```

| GPIO | LED | Series resistor |
|------|-----|-----------------|
| 17 | LED 1 | 330 Ω |
| 19 | LED 2 | 330 Ω |
| 23 | LED 3 | 330 Ω |

Adjust the series resistor for your LED's forward voltage and desired brightness. 330Ω is a conservative starting point at 3.3V.

> **Active vs passive:** `LED_ACTIVE_HIGH true` means the GPIO goes HIGH to turn the LED on. If your circuit pulls the LED anode high via a resistor and the GPIO drives the cathode, set `LED_ACTIVE_HIGH false` in `switch_panel.h`.

**Controlling LEDs from the bus:**
```
# CAN frame: 0x102 [target_node_id, mask, state]
102 01 07 07    # switch panel (0x01): all 3 LEDs on
102 01 07 00    # switch panel (0x01): all 3 LEDs off
102 01 01 01    # LED 1 on only
102 01 06 02    # LED 2 on, LED 3 off (mask touches only bits 1-2)
102 FF 07 07    # broadcast: all LEDs on all nodes that have ENABLE_LEDS
```

### 3.8 BTN3 / BTN4 External Pull-ups

BTN3 and BTN4 were moved to GPIO 36 and 39 to free GPIO 19 and 23 for LED outputs. These pins are input-only and have **no internal pull-up** on the ESP32 — the firmware's `INPUT_PULLUP` request is silently ignored.

Wire a 10kΩ resistor from each pin to 3V3:

```
3V3 ──[10kΩ]──┬── GPIO 36   BTN3 connects pin to GND when pressed
               └── BTN3

3V3 ──[10kΩ]──┬── GPIO 39   BTN4 connects pin to GND when pressed
               └── BTN4
```

### 3.9 Full Switch Panel Pin Summary

```
ESP32 #1 (Switch Panel)
─────────────────────────────────────────
3V3  ──── encoder VCC, BTN3/BTN4 10kΩ pull-ups
GND  ──── switch commons, encoder GND, buzzer (–), LED cathodes
GPIO 4   CAN RX  ←── CAN bus
GPIO 5   CAN TX  ──→ CAN bus
GPIO 13  SW6
GPIO 14  BTN1  (encoder select/back button for menu)
GPIO 16  Buzzer (+)  via optional 100Ω series resistor
GPIO 17  LED 1  ──→ 330Ω ──→ LED anode
GPIO 18  BTN2  (All OFF)
GPIO 19  LED 2  ──→ 330Ω ──→ LED anode
GPIO 21  LCD SDA
GPIO 22  LCD SCL
GPIO 23  LED 3  ──→ 330Ω ──→ LED anode
GPIO 25  SW1
GPIO 26  SW2
GPIO 27  SW3
GPIO 32  SW4
GPIO 33  SW5
GPIO 34  ENC GA  (CJMCU-111 — no external pull-up needed)
GPIO 35  ENC GB  (CJMCU-111 — no external pull-up needed)
GPIO 36  BTN3   requires external 10kΩ pull-up to 3V3
GPIO 39  BTN4   requires external 10kΩ pull-up to 3V3
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

This gives a divider ratio of 5.545 (10k + 2.2k / 2.2k). Adjust `VBAT_DIVIDER_RATIO` in `firmware/configs/relay_controller.h` to match your actual resistors. Telemetry is reported in centvolts on CAN ID 0x300.

### 4.5 RPM Sensor (PC817C Optocoupler)

The relay controller reads engine RPM from the coil's negative terminal via a PC817C optocoupler. The optocoupler isolates the noisy ignition circuit from the ESP32.

```
Coil (–) ──[270Ω]──── PC817C pin 1 (anode)
                       PC817C pin 2 (cathode) ──── GND

3.3V ────[10kΩ]──┬─── PC817C pin 3 (collector)
                 └─── GPIO 35 (RPM_PIN)
                       PC817C pin 4 (emitter) ──── GND
```

The 270Ω resistor limits current through the PC817C LED (~14 mA at 12V — within the PC817C's 50 mA rating). The 10kΩ pull-up holds the output HIGH between coil fires; each fire pulls it LOW and the ESP32 counts the falling edge as an interrupt.

**GPIO 35 is input-only** — no internal pull-up. The external 10kΩ provides the required pull-up. Do not omit it.

| PC817C Pin | Signal | Connection |
|-----------|--------|------------|
| 1 (anode) | LED + | Coil (–) via 270Ω resistor |
| 2 (cathode) | LED – | GND |
| 3 (collector) | Output | GPIO 35 + 10kΩ pull-up to 3.3V |
| 4 (emitter) | GND | GND |

RPM is broadcast on CAN ID `0x304` every 500 ms as a little-endian uint16 (divide raw value by 1 for RPM). `RPM_CYLINDERS` in `relay_controller.h` defaults to 8 for a V8 — change to 6 or 4 for other engines.

### 4.6 Full Relay Controller Pin Summary

```
ESP32 #2 (Relay Controller)
─────────────────────────────────────────
3V3  ──── 10kΩ pull-up for RPM_PIN
GND  ──── ULN2803 GND, relay coil return, PC817C cathode/emitter
GPIO 4   CAN RX  ←── CAN bus
GPIO 5   CAN TX  ──→ CAN bus
GPIO 16  Relay 1 ──→ ULN2803 IN1 (or 2N3904 base via 270Ω)
GPIO 17  Relay 2 ──→ ULN2803 IN2
GPIO 18  Relay 3 ──→ ULN2803 IN3
GPIO 19  Relay 4 ──→ ULN2803 IN4
GPIO 21  Relay 5 ──→ ULN2803 IN5  (horn — 30 s safety cutoff)
GPIO 22  Relay 6 ──→ ULN2803 IN6
GPIO 34  Vbat ADC ←── voltage divider (10 kΩ / 2.2 kΩ)
GPIO 35  RPM input ←── PC817C collector (10kΩ pull-up to 3V3)
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

## 5a. GPS Module Wiring (placement TBD)

The GPS module provides vehicle speed and heading over NMEA via UART. Only the module's TX line is needed — connect it to the ESP32 RX pin. The module's RX pin can be left unconnected for basic NMEA reading.

```
GPS module VCC  ──── 3.3V (check module — some need 5V via VIN)
GPS module GND  ──── GND
GPS module TX   ──── GPS_RX_PIN (ESP32 UART RX)
GPS module RX   ──── not connected (optional for sending config commands)
```

Common modules (u-blox Neo-6M / Neo-8M) default to 9600 baud, NMEA output. Set `GPS_BAUD`, `GPS_RX_PIN`, `GPS_TX_PIN`, and `GPS_SERIAL_NUM` in the node config header. Speed and heading broadcast on CAN ID `0x305`:

| Bytes | Field | Units |
|-------|-------|-------|
| 0–1 | speed (uint16 LE) | 0.1 mph — divide by 10 |
| 2–3 | heading (uint16 LE) | 0.1 degrees true — divide by 10 |
| 4 | flags | bit 0 = fix valid, bit 1 = speed valid, bit 2 = heading valid |

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

### 8.2 Configure the Target Node

Each node has a config header in `firmware/configs/`. Open the appropriate one and set the transceiver mode:

```cpp
#define USE_CAN_TRANSCEIVER 0   // bench mode — open-drain TX, pull-up required
// or
#define USE_CAN_TRANSCEIVER 1   // production mode — transceivers on GPIO 5/4
```

All three nodes must agree on this setting.

### 8.3 Compile and Upload

The Makefile copies the right config header, then compiles the single unified sketch.

**arduino-cli (recommended):**

```bash
# Find your serial ports
ls /dev/cu.usbserial-* /dev/cu.wchusbserial-* /dev/cu.SLAB_USBtoUART 2>/dev/null

# Compile all three nodes in sequence
make all

# Upload (substitute your actual port paths)
make upload-relay_controller  PORT=/dev/cu.usbserial-XXXX
make upload-switch_panel      PORT=/dev/cu.usbserial-YYYY
make upload-viper_interface   PORT=/dev/cu.usbserial-ZZZZ
```

**Arduino IDE:**
Copy the desired config manually before opening the sketch:
```bash
cp firmware/configs/switch_panel.h firmware/accessory_node/node_config.h
```
Then open `firmware/accessory_node/accessory_node.ino`, select **Tools → Board → ESP32 Dev Module**, choose the correct port, and click Upload. Repeat for each node.

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
9. Press SW1 — a `SWITCH_EVENT` fires, the default rule triggers a `RELAY_CMD`, relay 1 clicks on. Press again to turn it off. The web console logs `SWITCH_EVENT`, `RELAY_CMD`, and `RELAY_STATUS` in sequence.
10. `:viper lock` from the web console should cause the alarm to arm (Viper status LED blinks / chirps).

---

## 10. Web Console

Each node runs an HTTP server with a captive DNS portal on its `AccessoryBus` AP. Any device on the same WiFi will reach the same bus — frames are shared across nodes via ESP-NOW.

**Access:** Connect to WiFi SSID `AccessoryBus` (configurable via `AP_SSID` in the node config) → browser auto-opens, or navigate to `http://192.168.4.1`.

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
| `:readcfg relay` | Dump all relay max-on-ms settings |
| `:save relay` | Persist relay config to NVS |
| `:reset relay` | Factory-reset relay config |
| `:cfgrelay <idx> maxon <ms> [!]` | Set per-relay safety auto-off (0 = no limit) |

Trailing `!` on `:cfgrelay` persists the change to NVS immediately.

### 10.3 Control Tab

The **Control** tab provides a graphical panel that adapts to the connected node:

**Switch panel — Switch Inputs, Buttons, LEDs**

Physical switch state tiles (tracked from SWITCH_EVENT frames) reflect the actual GPIO inputs — clicking one sends a SWITCH_EVENT to simulate a toggle. Momentary button tiles send a press/release pair with a 150 ms gap when clicked. LED status dots reflect the last LED_STATUS broadcast for this node; clicking a dot sends a LED_CMD to toggle that LED. Relays are not shown here — manage relays from the relay controller web console.

**Relay controller — Relays**

Six tiles showing each relay's live state (green = ON). State tracks automatically from RELAY_STATUS (0x101) broadcasts — updates even while you're on the CAN Frames tab. Each tile has a **Toggle** button; there's also a global **All OFF** button.

**Viper interface only — Alarm**

**Lock / Arm**, **Unlock / Disarm**, and **Remote Start** buttons. The last raw Viper status response (0x511) is shown below.

### 10.4 Rules Tab

The **Rules** tab lists all rules stored in NVS. Each rule shows a human-readable description of its trigger and action. You can:
- **Add** a new rule using the inline editor (pick trigger ID, byte conditions, action kind, args)
- **Edit** an existing rule in place
- **Delete** a single rule
- **Factory Reset** — clears all rules and restores the compiled `RULES_DEFAULT_INIT` defaults

Changes persist to NVS immediately.

### 10.4 Force WiFi-Only Mode

The checkbox in the page header cuts wired CAN TX. Useful to test that the ESP-NOW fallback path works. Uncheck to restore the wired bus. (This setting is RAM-only and resets on power cycle.)

---

## 11. LCD Menu System

The switch panel has a built-in menu driven by the rotary encoder and BTN1 (GPIO 14).

### Navigation

| Input | Action |
|-------|--------|
| Long-press BTN1 from idle (≥ 600 ms) | Enter menu |
| Short-press BTN1 in menu | Navigate: enter submenu, or confirm "← Back" / "← Exit" |
| Long-press BTN1 on an action item | Execute / toggle (relay, Viper command, backlight) |
| Rotate encoder CW / CCW | Scroll down / up |
| 15 s no input | Auto-exit back to status display |

### Menu Structure

Menu opens on the first real item. **"← Exit"** (top level) and **"← Back"** (submenus) are always the **last** item in each list — scroll past the items to reach it. Action items require a **long-press** to execute; short-press is navigation only.

```
[idle: CAN status + last event]
│
└─ MENU  (long-press BTN1)
   ├─ Relays
   │   ├─ Relay 1–6    long press → toggle ON/OFF
   │   └─ ← Back       short press → return to MENU
   ├─ Viper
   │   ├─ Lock / Arm       long press → send command
   │   ├─ Unlock/Disarm    long press → send command
   │   ├─ Remote Start     long press → send command
   │   └─ ← Back
   ├─ Bus Status       live TWAI error counters (read-only)
   │   └─ ← Back
   ├─ Display
   │   ├─ Backlight    long press → toggle ON/OFF
   │   └─ ← Back
   ├─ WiFi             enable/disable WiFi per node
   │   ├─ SwitchPnl   long press → toggle (self: restarts immediately)
   │   ├─ RelayCtr    long press → toggle (sends CONFIG_WRITE, node restarts)
   │   ├─ Viper       long press → toggle
   │   └─ ← Back
   └─ ← Exit           short press → exit menu
```

### LCD Layout

**Idle (auto-display):**
```
C✦W✦ [✦-----]       ← row 0: CAN status icon, WiFi status icon, relay bitmap
Headlights ON        ← row 1: last meaningful event
```

Row 0 format: `C` = CAN bus health, `W` = WiFi/ESP-NOW peer seen, icon = filled (link up) or hollow (link down), followed by the 6-relay bitmap. If CGRAM slots were exhausted by relay icons, `+`/`-` ASCII is used as fallback for the status icons.

Custom icons and labels for each relay are defined in `firmware/configs/switch_panel.h` with `RELAY_n_LABEL`, `RELAY_n_ICON_ON`, and `RELAY_n_ICON_OFF` macros. HD44780 has 8 CGRAM slots; slot 0 is reserved (maps to C null terminator), and 2 slots are consumed by the shared CAN/WiFi status icons, leaving up to **5 custom relay icons** total.

**Navigating top-level menu:**
```
MENU  [2/6]
→ Viper
```

**Inside Relays submenu, relay 1 selected (ON):**
```
Relays ✦-----
→ ✦ Headlights ON    ← long-press BTN1 to toggle
```

**Back item selected:**
```
Relays ✦-----
← Back               ← short-press BTN1 to go back
```

---

## 12. Relay Customization (Labels and Icons)

Each relay can have a human-readable label and custom HD44780 CGRAM icons for its ON and OFF states. These are defined at compile time in the node config header.

```cpp
// firmware/configs/switch_panel.h
#define RELAY_1_LABEL    "Headlights"
#define RELAY_1_ICON_ON  {0b10101, 0b10101, 0b10101, 0b00000, 0b11111, 0b11111, 0b01110, 0b00000}
#define RELAY_1_ICON_OFF {0b00000, 0b00000, 0b00000, 0b11111, 0b10001, 0b11111, 0b01110, 0b00000}
```

Each icon is an 8-byte HD44780 5×8 pixel bitmap. Omit any macro to use the defaults: label = "Relay N", ON = full block (`█`), OFF = `-`.

**Limits:**
- Up to **5** custom relay icons total. HD44780 has 8 CGRAM slots; slot 0 is reserved (maps to C null terminator, truncates `snprintf`), and 2 slots are used by the shared CAN/WiFi status icons loaded at the end of `lcd_setup()`.
- Labels appear in the LCD menu Relays submenu and in the web UI relay tiles (relay controller node).
- Icons appear on the LCD idle display and the Relays submenu state bitmap.

---

## 13. Runtime Configuration — Rules Engine

Switch-to-relay/LED/alarm mappings are stored in NVS as **rules** and can be changed without reflashing. Rules are managed in the **Rules** tab of the web console.

### 13.1 How Rules Work

Each rule has:
- **Trigger** — a CAN frame ID to watch for, plus up to two byte conditions (`byte_index == value & mask`). A condition is skipped when its mask is `0x00`.
- **Action** — what to do when the trigger fires: toggle a relay, turn a relay on/off, set all relays off, control an LED, enable/disable WiFi, send a Viper command, or navigate the LCD menu.

Because `bus_tx()` self-echoes every outgoing frame, rules also fire on frames that *this node* sent — meaning a rule triggered by a RELAY_CMD can drive a secondary LED, and a rule triggered by RELAY_STATUS keeps LEDs in sync automatically.

### 13.2 Default Rules (switch_panel)

| Trigger | Action |
|---------|--------|
| SW1 press | Toggle relay 1 |
| SW2 press | Toggle relay 2 |
| SW3 press | Toggle relay 3 |
| SW4 press | Toggle relay 4 |
| SW5 press | Relay 5 ON |
| SW5 release | Relay 5 OFF |
| BTN1 press | Menu select (navigate / confirm) |
| BTN1 long-press | Menu enter (open from idle, or execute action item) |
| BTN2 press | All relays OFF |
| Relay 1 bit ON (RELAY_STATUS) | LED 1 ON on switch_panel |
| Relay 1 bit OFF (RELAY_STATUS) | LED 1 OFF on switch_panel |

These are compiled into `RULES_DEFAULT_INIT` in `firmware/configs/switch_panel.h` and applied when there are no rules in NVS (first boot) or after a factory reset.

### 13.3 Adding / Editing Rules at Runtime

Use the **Rules** tab in the web console — no CLI alias exists for rule editing. The editor presents dropdowns for trigger ID, byte index, expected value, mask, action kind, and args.

To factory-reset all rules to compiled defaults, use the **Reset to Defaults** button in the Rules tab.

### 13.4 Set Relay Safety Timeout

```
:cfgrelay <relay_index> maxon <milliseconds> [!]
```

Relay index 0–5 maps to relays 1–6. `0` means no limit.

```
:cfgrelay 4 maxon 5000     relay 5 (horn) cuts off after 5 s
:cfgrelay 4 maxon 0 !      remove the limit and save
```

Changes without `!` are RAM-only and lost on reboot. Save with:
```
:save relay
```

---

## 14. CAN Frame Reference

| ID | Name | Direction | Payload |
|----|------|-----------|---------|
| 0x100 | RELAY_CMD | Any → relay_controller | `[mask, state]` — bit N = relay N+1; only mask bits change |
| 0x101 | RELAY_STATUS | relay_controller → all | `[bitmap]` — bit N = relay N+1 on/off; 5 Hz |
| 0x102 | LED_CMD | Any → target node | `[target_node_id, mask, state]` — 0xFF target = broadcast |
| 0x103 | LED_STATUS | Target node → all | `[node_id, bitmap]` — sent on change |
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

**Config keys:** `0x20` CFG_KEY_RELAY_MAX_ON_MS (per-relay safety auto-off timeout)

---

## 15. Bench Mode vs Production Mode

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

## 16. Troubleshooting

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

- Try I2C address `0x3F` instead of `0x27` (change `LCD_I2C_ADDR` in `firmware/configs/switch_panel.h` or `viper_interface.h`).
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

GPIO 13 is a strapping pin on some ESP32 modules. If SW6 triggers spurious events during power-on, move it to another GPIO and update `INPUT_PINS_INIT` in `firmware/configs/switch_panel.h`.
