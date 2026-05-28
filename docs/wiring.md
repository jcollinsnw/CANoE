# CANoE — Wiring Guide

Hardware setup for all nodes: bill of materials, pinouts, CAN bus backbone, and power distribution.

---

## Table of Contents

1. [Bill of Materials](#1-bill-of-materials)
2. [Switch Panel Node (0x01)](#2-switch-panel-node-0x01)
3. [Relay Controller Node (0x02)](#3-relay-controller-node-0x02)
4. [Viper Interface Node (0x03)](#4-viper-interface-node-0x03)
5. [ECU Node (0x04)](#5-ecu-node-0x04)
6. [Bridge Node (0x05)](#6-bridge-node-0x05)
7. [Cardputer Node (0x06)](#7-cardputer-node-0x06)
8. [GPS Module](#8-gps-module)
9. [CAN Bus Backbone](#9-can-bus-backbone)
10. [Power Distribution](#10-power-distribution)
11. [Bench Mode vs Production Mode](#11-bench-mode-vs-production-mode)

---

## 1. Bill of Materials

### Per Node (× 5)
| Qty | Part | Notes |
|-----|------|-------|
| 1 | ESP32 WROOM-32 dev board | 38-pin variant preferred for GPIO count |
| 1 | USB-to-serial cable | For initial flashing and serial monitor |

### Cardputer Node
| Qty | Part | Notes |
|-----|------|-------|
| 1 | M5Stack Cardputer (ESP32-S3) | Built-in TFT, keyboard, LiPo battery |
| 1 | TJA1051T/3 or SN65HVD230 CAN transceiver | *(optional — can run ESP-NOW only)* |

### CAN Bus (Production Mode)
| Qty | Part | Notes |
|-----|------|-------|
| 2–5 | TJA1051T/3 or SN65HVD230 CAN transceiver | One per node on the wire |
| 1 | Twisted pair wire | CANH / CANL, 22–24 AWG |
| 2 | 120 Ω resistor, 0.25 W | One at each physical end of the bus |

### CAN Bus (Bench Mode, short runs only)
| Qty | Part | Notes |
|-----|------|-------|
| 1 | 1 kΩ–4.7 kΩ resistor | Pull-up on shared TX wire to 3V3 |

### Switch Panel Node
| Qty | Part | Notes |
|-----|------|-------|
| 3 | Latching toggle or rocker switch | Normally open, one pole to GPIO, other to GND |
| 7 | Momentary push-button | Same wiring as switches |
| 1 | Rotary encoder | CJMCU-111 (EC11-based); has onboard 3.3 kΩ pull-ups — connect module VCC to 3V3, no external resistors needed |
| 1 | HD44780-compatible 16×2 LCD with PCF8574 I2C backpack | Default I2C address 0x27; try 0x3F if blank |
| 1 | 100 µF electrolytic capacitor, 10 V+ | Decoupling cap across LCD VCC/GND — prevents HD44780 losing state on power glitches |
| 1 | Passive piezo buzzer | 3.3 V-compatible; positive leg to GPIO 16 via optional 100Ω resistor, negative to GND |
| 1 | 100 Ω resistor, ¼ W *(optional)* | Series resistor on buzzer to reduce volume |
| 3 | LED (any colour) | Status LEDs on GPIO 17, 19, 23; controlled over CAN |
| 3 | 330 Ω resistor, ¼ W | Series resistor for each LED |
| 2 | 10 kΩ resistor, ¼ W | Pull-up to 3V3 for BTN6 (GPIO 36) and BTN7 (GPIO 39) — no internal pull-up on these pins |
| 2 | 10 kΩ resistor, ¼ W | *(battery ADC option)* Voltage divider top leg (GPIO 36 + GPIO 39) |
| 2 | 2.2 kΩ resistor, ¼ W | *(battery ADC option)* Voltage divider bottom leg |

### Relay Controller Node
| Qty | Part | Notes |
|-----|------|-------|
| 1 | ULN2803A Darlington array IC | Low-side relay driver, 8 channels (only 6 used) |
| 6 | 12 V relay module (or bare relay + flyback diode) | Coil current ≤ 500 mA per channel |
| 6 | 10 A automotive fuse + fuse holder | One per relay output |
| 2 | 10 kΩ resistor | Voltage divider top legs (battery on GPIO 34, aux battery on GPIO 36) |
| 2 | 2.2 kΩ resistor | Voltage divider bottom legs |
| 1 | PC817C optocoupler | RPM input isolation |
| 1 | 270 Ω resistor | PC817C LED current limiter |
| 1 | 10 kΩ resistor | PC817C pull-up to 3.3V (GPIO 35) |
| 1 | 10 kΩ resistor | Coil-sense voltage divider top leg (GPIO 39) |
| 1 | 2.2 kΩ resistor | Coil-sense voltage divider bottom leg |

### Viper Interface Node
| Qty | Part | Notes |
|-----|------|-------|
| 1 | 3.3 V ↔ 5 V bidirectional level shifter | 2-channel minimum (TX + RX) |
| — | Viper 5305V alarm (or compatible) | Existing install |

### ECU Node

See [§5.2 ECU Node Bill of Materials](#52-bill-of-materials) for the full parts list. Key items: 3× IRLZ44N logic-level MOSFET, 3× 1N5822 Schottky flyback diode, 1× PC817C optocoupler, 1× MPX4250AP MAP sensor, 2× GM NTC thermistor (10 kΩ), wideband O2 controller + LSU 4.9 sensor.

### Bridge Node
| Qty | Part | Notes |
|-----|------|-------|
| — | *(no additional hardware)* | Just an ESP32 + CAN transceiver; no sensors or outputs |

---

## 2. Switch Panel Node (0x01)

### 2.1 CAN Bus

| ESP32 Pin | Signal | Goes to |
|-----------|--------|---------|
| GPIO 5 | CAN TX | Transceiver TXD (production) or shared CAN wire (bench) |
| GPIO 4 | CAN RX | Transceiver RXD (production) or shared CAN wire (bench) |

### 2.2 Switches (SW1–SW3, latching)

All inputs use `INPUT_PULLUP`. Wire one terminal to the GPIO and the other to GND. Switch closed = LOW = pressed.

| Label | ESP32 Pin | Default Rule |
|-------|-----------|--------------|
| SW1 | GPIO 25 | Toggles relay 2 (fuel pump) |
| SW2 | GPIO 26 | Toggles relay 3 (choke) |
| SW3 | GPIO 27 | Spare |

### 2.3 Buttons (BTN1–BTN7, momentary)

Same wiring as switches. GPIO 36 and 39 have no internal pull-up — see §2.8.

| Label | ESP32 Pin | Default Rule |
|-------|-----------|--------------|
| BTN1 | GPIO 32 | Hold → relay 5 ON (horn); release → relay 5 OFF |
| BTN2 | GPIO 33 | Press → toggle relay 1 (headlights) |
| BTN3 | GPIO 13 | Press → all relays off. **GPIO 13 is a strapping pin — move if boot issues** |
| BTN4 | GPIO 14 | Long-press → enter/back menu; short-press → select |
| BTN5 | GPIO 18 | Spare |
| BTN6 | GPIO 36 | Spare (external 10kΩ pull-up to 3V3 required) |
| BTN7 | GPIO 39 | Spare (external 10kΩ pull-up to 3V3 required) |

All switch-to-action mappings are driven by the rules engine (`RULES_DEFAULT_INIT` in `switch_panel.h`) and fully reassignable at runtime via the Rules tab.

### 2.4 Rotary Encoder

Module: **CJMCU-111** (EC11-based). Has onboard 3.3 kΩ pull-up resistors on GA and GB — no external resistors required. Connect module VCC to the ESP32 3V3 rail.

> **No SW pin:** The CJMCU-111 physically clicks when you press the shaft but the switch contact is not wired to any pin header. BTN4 (GPIO 14) serves as the encoder button for the LCD menu.

```
Encoder GA ──── GPIO 34   (onboard pull-up via module VCC)
Encoder GB ──── GPIO 35   (onboard pull-up via module VCC)
Encoder VCC ─── 3V3
Encoder GND ─── GND
```

### 2.5 LCD (HD44780, PCF8574 I2C Backpack)

| LCD Module Pin | ESP32 Pin | Notes |
|----------------|-----------|-------|
| VCC | 5V (VIN) | Most HD44780 backpacks require 5 V; I2C runs at 3V3 via the backpack |
| GND | GND | — |
| SDA | GPIO 21 | I2C data |
| SCL | GPIO 22 | I2C clock |

Default I2C address is `0x27`. Try `0x3F` if the display stays blank after boot.

> **Decoupling capacitor:** Solder a **100 µF electrolytic capacitor** across the LCD VCC and GND pins. Power glitches from relay switching can momentarily dip the LCD supply and cause the HD44780 to lose its init state. The firmware self-recovers within 30 s, but the cap prevents it from happening.

### 2.6 Piezo Buzzer

Requires a **passive** (not active) buzzer — the firmware generates frequencies using Arduino `tone()`.

```
GPIO 16 ──[100Ω]──── Buzzer (+)
GND ─────────────── Buzzer (–)
```

The 100Ω series resistor is optional — it reduces volume. `BUZZER_PIN` must not be an input-only GPIO (avoid 34/35/36/39).

| Event | Sound |
|-------|-------|
| Menu enter | Rising two-tone (E5 → C6) |
| Menu exit | Falling two-tone (C6 → E5) |
| Menu scroll | Short tick (G5, 18 ms) |
| Relay ON | Rising chirp (G5 → C6) |
| Relay OFF | Falling chirp (C6 → E5) |
| All OFF | Descending three-note sweep |

### 2.7 Status LEDs

Three LEDs on GPIO 17, 19, 23 controlled via CAN frame 0x102 (LED_CMD).

```
GPIO 17 ──[330Ω]──── LED 1 anode
GPIO 19 ──[330Ω]──── LED 2 anode     LED cathodes ──── GND
GPIO 23 ──[330Ω]──── LED 3 anode
```

Adjust the series resistor for your LED's forward voltage and desired brightness.

### 2.8 BTN6 / BTN7 External Pull-ups

GPIO 36 and 39 are input-only with **no internal pull-up**. `INPUT_PULLUP` is silently ignored.

```
3V3 ──[10kΩ]──┬── GPIO 36   (BTN6 pulls to GND when pressed)
               └── BTN6

3V3 ──[10kΩ]──┬── GPIO 39   (BTN7 pulls to GND when pressed)
               └── BTN7
```

> **Note:** When `ENABLE_BATTERY` is configured on the switch panel, GPIO 36 and 39 are reassigned as battery voltage ADC inputs (see §2.9). In that case BTN6/BTN7 are unavailable.

### 2.9 Battery Voltage Monitor (Optional)

Two independent voltage dividers for primary and auxiliary battery monitoring. Both GPIOs are input-only ADC1 pins — required because ADC2 conflicts with WiFi.

```
Primary (factory) battery
12 V rail ──[10 kΩ]──┬──[2.2 kΩ]── GND
                     └── GPIO 36

Auxiliary (accessory) battery
12 V rail ──[10 kΩ]──┬──[2.2 kΩ]── GND
                     └── GPIO 39
```

Divider ratio: 5.545 ((10k + 2.2k) / 2.2k). Adjust `VBAT_DIVIDER_RATIO` and `VBAT2_DIVIDER_RATIO` in `switch_panel.h` to match your actual resistors. Telemetry is reported in centvolts on CAN ID 0x300: primary battery in bytes 0–1, auxiliary in bytes 4–5.

> **Pin conflict:** GPIO 36/39 are shared with BTN6/BTN7 spare buttons. When `ENABLE_BATTERY` is defined in the switch panel config, these pins read battery voltage instead. The buttons cannot be used simultaneously.

### 2.10 Full Switch Panel Pin Summary

```
ESP32 #1 (Switch Panel)
─────────────────────────────────────────
3V3  ──── encoder VCC, BTN6/BTN7 10kΩ pull-ups
GND  ──── switch/button commons, encoder GND, buzzer (–), LED cathodes
GPIO 4   CAN RX  ←── CAN bus
GPIO 5   CAN TX  ──→ CAN bus
GPIO 13  BTN3  (All OFF — strapping pin, move if boot issues)
GPIO 14  BTN4  (menu select/back)
GPIO 16  Buzzer (+) via optional 100Ω resistor
GPIO 17  LED 1  ──→ 330Ω ──→ LED anode
GPIO 18  BTN5  (spare)
GPIO 19  LED 2  ──→ 330Ω ──→ LED anode
GPIO 21  LCD SDA
GPIO 22  LCD SCL
GPIO 23  LED 3  ──→ 330Ω ──→ LED anode
GPIO 25  SW1  (Fuel Pump)
GPIO 26  SW2  (Choke)
GPIO 27  SW3  (spare)
GPIO 32  BTN1  (Horn — hold)
GPIO 33  BTN2  (Headlights toggle)
GPIO 34  ENC GA  (CJMCU-111 — no external pull-up needed)
GPIO 35  ENC GB  (CJMCU-111 — no external pull-up needed)
GPIO 36  BTN6 / Vbat ADC  (spare button OR primary battery voltage divider)
GPIO 39  BTN7 / Vbat2 ADC (spare button OR auxiliary battery voltage divider)
5V (VIN) LCD VCC
```

---

## 3. Relay Controller Node (0x02)

### 3.1 CAN Bus

GPIO 5 = TX, GPIO 4 = RX. Same as all other nodes.

### 3.2 Relay Driver (ULN2803A)

The ULN2803A is an 8-channel low-side Darlington driver. ESP32 drives inputs HIGH to energise a relay coil.

```
ESP32 GPIO ──→ ULN2803 Input ──→ (internal NPN) ──→ Output ──→ Relay coil (–)
                                                               Relay coil (+) ──→ 12 V
                                              ULN2803 COM ──→ 12 V  (flyback diode rail)
```

| Relay # | ESP32 Pin | Default Load |
|---------|-----------|--------------|
| 1 | GPIO 16 | Configurable |
| 2 | GPIO 17 | Configurable |
| 3 | GPIO 18 | Configurable |
| 4 | GPIO 19 | Configurable |
| 5 | GPIO 21 | **Horn** (30 s safety cutoff) |
| 6 | GPIO 22 | Configurable |

> **ULN2803 COM pin:** Connect to the 12 V rail. The internal flyback diodes clamp inductive spikes from relay coils back to this rail. Do not leave it floating.

### 3.3 Relay Output Connections

```
12 V ──→ Fuse ──→ Relay NO contact ──→ Load ──→ GND
                   Relay COM ──→ 12 V after fuse
```

### 3.4 Battery Voltage Monitor

> Both the relay controller and the switch panel can run `mod_battery`. The relay controller's config (`relay_controller.h`) has `ENABLE_BATTERY` defined by default since the relay node also runs `ENABLE_FUEL_PUMP_SAFETY` and the LV cutoff roadmap item depends on battery voltage. The switch panel's `mod_battery` config remains available — both broadcast on `TELEMETRY (0x300)` independently if both are wired.

Two independent dividers — one per battery rail. Both GPIOs are input-only with no pull-up, making them clean ADC inputs.

```
Primary (factory) battery
12 V rail ──[10 kΩ]──┬──[2.2 kΩ]── GND
                     └── GPIO 34

Auxiliary (accessory) battery
12 V rail ──[10 kΩ]──┬──[2.2 kΩ]── GND
                     └── GPIO 36
```

Divider ratio: 5.545 ((10k + 2.2k) / 2.2k). Adjust `VBAT_DIVIDER_RATIO` and `VBAT2_DIVIDER_RATIO` in the node config to match your actual resistors. Telemetry is reported in centvolts on CAN ID 0x300: primary battery in bytes 0–1, auxiliary in bytes 4–5.

### 3.5 RPM Sensor (PC817C Optocoupler)

The PC817C isolates the noisy ignition circuit from the ESP32. Tap the coil's negative terminal (same point as a traditional tachometer).

```
Coil (–) ──[270Ω]──── PC817C pin 1 (anode)
                       PC817C pin 2 (cathode) ──── GND

3.3V ────[10kΩ]──┬─── PC817C pin 3 (collector)
                 └─── GPIO 35 (RPM_PIN)
                       PC817C pin 4 (emitter) ──── GND
```

**GPIO 35 is input-only** — no internal pull-up. The external 10kΩ is required.

| PC817C Pin | Signal | Connection |
|-----------|--------|------------|
| 1 (anode) | LED + | Coil (–) via 270Ω resistor |
| 2 (cathode) | LED – | GND |
| 3 (collector) | Output | GPIO 35 + 10kΩ pull-up to 3.3V |
| 4 (emitter) | GND | GND |

`RPM_CYLINDERS` in `relay_controller.h` defaults to 8 for a V8 — change to 6 or 4 as needed.

### 3.6 Ignition Coil Voltage Sense

Reads the ignition coil's + side voltage so the relay node knows whether the key is in the ignition. Used by `mod_fuel_pump` as the COIL gate (paired with the RPM gate above). Tap the coil + line **after** the dash ballast resistor — that's the ~9 V side when the key is in RUN, ~12 V during cranking, 0 V key-out. See [Power architecture in CLAUDE.md](../CLAUDE.md#power-architecture-important-context) for why this matters.

```
Coil + ──[10 kΩ]──┬──[2.2 kΩ]── GND
                   └── GPIO 39 (IGN_COIL_ADC_PIN)
```

| Pin | Signal | Connection |
|-----|--------|------------|
| GPIO 39 | Coil voltage ADC | Divider midpoint. ADC1 input-only — no internal pull-up, none needed (divider sets the voltage). |

Divider ratio: 5.545 ((10k + 2.2k) / 2.2k). At 13 V input → 2.34 V at the ADC pin, safely under 3.3 V. Adjust `IGN_COIL_DIVIDER_RATIO` in `relay_controller.h` if you use different resistors.

> **Tap point — coil + NOT coil –:** The coil's negative terminal is the high-voltage switching side used for tachometer signals (and for the PC817C RPM input above). Coil + is the steady supply side fed from the key switch through the dash ballast. They are different signals; do not swap them.

The on/off thresholds (`IGN_COIL_ON_THRESHOLD_CV` = 600, `IGN_COIL_OFF_THRESHOLD_CV` = 400) have hysteresis between them so the boolean state doesn't chatter around the threshold during cranking spikes.

### 3.7 Full Relay Controller Pin Summary

```
ESP32 #2 (Relay Controller)
─────────────────────────────────────────
3V3  ──── 10kΩ pull-up for RPM_PIN
GND  ──── ULN2803 GND, relay coil return, PC817C cathode/emitter
GPIO 4   CAN RX  ←── CAN bus
GPIO 5   CAN TX  ──→ CAN bus
GPIO 16  Relay 1 ──→ ULN2803 IN1
GPIO 17  Relay 2 ──→ ULN2803 IN2
GPIO 18  Relay 3 ──→ ULN2803 IN3
GPIO 19  Relay 4 ──→ ULN2803 IN4
GPIO 21  Relay 5 ──→ ULN2803 IN5  (horn — 30 s safety cutoff)
GPIO 22  Relay 6 ──→ ULN2803 IN6
GPIO 34  Vbat ADC  ←── voltage divider (10 kΩ / 2.2 kΩ) — primary battery
GPIO 35  RPM input ←── PC817C collector (10kΩ pull-up to 3V3)
GPIO 36  Vbat2 ADC ←── voltage divider (10 kΩ / 2.2 kΩ) — auxiliary battery
GPIO 39  Coil ADC  ←── voltage divider (10 kΩ / 2.2 kΩ) — coil + after ballast resistor
```

---

## 4. Viper Interface Node (0x03)

### 4.1 CAN Bus

GPIO 5 = TX, GPIO 4 = RX.

### 4.2 Viper 5305V Serial Connection

The Viper alarm communicates at 9600 baud, 8N1, 5 V TTL logic. Use a bidirectional level shifter on both TX and RX lines.

```
ESP32 3V3 ──→ Level Shifter LV rail
Viper 5V  ──→ Level Shifter HV rail

ESP32 GPIO 16 (UART2 TX) ──→ LV1 ─── HV1 ──→ Viper Serial RX
ESP32 GPIO 17 (UART2 RX) ←── LV2 ─── HV2 ←── Viper Serial TX

ESP32 GND ──→ Level Shifter GND  (also tie to Viper GND — shared ground required)
```

> **Finding Viper serial pins:** The 5305V has a data/programming port (usually a 4-pin header near the main harness). Use `ViperESP2::sniff()` during initial setup to confirm byte order before enabling commands.

### 4.3 Full Viper Interface Pin Summary

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

## 5. ECU Node (0x04)

Reads MAP, TPS, CLT, IAT, RPM, and wideband O2. Drives either a carburetor mixture-control solenoid (closed-loop carb mode) or dual throttle-body fuel injectors (TBI mode). Mode is selected by a switch on GPIO 13 and overrideable at runtime via CAN.

### 5.1 CAN Bus

GPIO 5 = TX, GPIO 4 = RX.

### 5.2 Bill of Materials

| Qty | Part | Notes |
|-----|------|-------|
| 3 | IRLZ44N logic-level N-MOSFET (TO-220) | **Must be logic-level** — fully switches at 3.3V. Standard IRF540/IRFZ44N will NOT reliably switch at 3.3V. |
| 3 | 1N5822 Schottky diode | Flyback suppression: cathode to +12V, anode to MOSFET drain |
| 3 | 100 Ω resistor, ¼ W | MOSFET gate series resistor — damps switching ringing |
| 3 | 10 kΩ resistor, ¼ W | MOSFET gate pull-down to GND |
| 6 | 100 kΩ resistor, ¼ W | 2:1 voltage dividers for MAP, TPS, WBO2 (two resistors each) |
| 2 | 10 kΩ resistor, ¼ W | NTC thermistor pull-up to 3.3V (CLT + IAT) |
| 1 | 10 kΩ resistor, ¼ W | Mode switch pull-up to 3.3V (GPIO 13) |
| 1 | 10 kΩ resistor, ¼ W | RPM optocoupler pull-up (GPIO 34) |
| 1 | PC817C optocoupler | RPM input isolation |
| 1 | 270 Ω resistor, ¼ W | PC817C LED current limiter |
| 1 | MPX4250AP MAP sensor | 0–250 kPa, 0.2–4.9V; common in GM/Ford TBI |
| 2 | GM-style 10 kΩ NTC thermistor | CLT + IAT; Beta=3540, R₀=2590Ω at 25°C |
| 1 | Wideband O2 controller | Innovate LC-2, AEM X-Series, or equivalent; must have 0–5V analog output |
| 1 | Wideband sensor (Bosch LSU 4.9) | Usually bundled with controller |
| 1 | TPS potentiometer | 0–5V wiper; built into many GM/Ford throttle bodies |
| 2 | Fuel injectors (750 cc/min each) | For a 351W V8 |
| 2 | 5 A automotive fuse + holder | One dedicated fuse per injector |
| 1 | 3 A automotive fuse + holder | Carb solenoid dedicated +12V |

### 5.3 Voltage Divider Wiring (MAP, TPS, WBO2)

All three 5V sensor outputs must be scaled to 0–3.3V.

```
Sensor 0–5V ──[100kΩ]──┬──[100kΩ]── GND
                         └── GPIO (ADC input, sees 0–2.5V)
```

| Signal | GPIO | Raw range | ADC range after divider |
|--------|------|-----------|------------------------|
| MAP (MPX4250AP) | 36 | 0.2–4.9 V | 0.1–2.45 V |
| TPS (pot wiper) | 39 | 0–5 V | 0–2.5 V |
| WBO2 (controller out) | 35 | 0–5 V | 0–2.5 V |

> **ADC1-only (critical):** When WiFi is active, ADC2 (GPIOs 0, 2, 4, 12–15, 25–27) is unavailable. All ECU analog inputs must be on ADC1 (GPIOs 32–39).

### 5.4 NTC Thermistor Wiring (CLT, IAT)

```
3.3V ──[10kΩ]──┬── GPIO 32 (CLT)      3.3V ──[10kΩ]──┬── GPIO 33 (IAT)
                └── NTC thermistor ── GND               └── NTC thermistor ── GND
```

The 3.3V pull-up keeps the ADC pin within the ESP32's safe input range at all temperatures. Do not use 5V.

| Sensor | GPIO | Placement |
|--------|------|-----------|
| CLT | 32 | Engine block coolant passage |
| IAT | 33 | Intake manifold or air cleaner |

Update `ECU_NTC_BETA` and `ECU_NTC_R0` in `ecu_node.h` for non-GM thermistors.

### 5.5 RPM Sensor (PC817C Optocoupler)

```
Ignition coil (–) ──[270Ω]──── PC817C pin 1 (LED +)
                                PC817C pin 2 (LED –) ──── GND

3.3V ──[10kΩ]──┬── PC817C pin 3 (collector)
                └── GPIO 34 (RPM_PIN)
                    PC817C pin 4 (emitter) ──── GND
```

**GPIO 34 has no internal pull-up.** The external 10kΩ is mandatory.

> **Distributor vs DIS:** `RPM_CYLINDERS = 8` is correct for a V8 distributor (4 coil fires per crankshaft revolution). Verify for DIS or coil-on-plug systems.

### 5.6 Mode Selection Switch

```
3.3V ──[10kΩ]──┬── GPIO 13
                └── Mode switch ── GND
```

Switch open → GPIO 13 HIGH → **injection mode**. Switch closed → **carb mode**.

> **GPIO 13 is a strapping pin.** A switch pulled LOW at power-on can cause erratic boot behavior. Use a normally-open switch so the pin defaults HIGH at boot. Override mode at runtime via CAN:
> ```
> 308 01 00 00 00    # ECU_CMD: set mode = carb (0)
> ```

### 5.7 Carb Mixture-Control Solenoid (Mode 0)

Air-bleed mixture-control solenoid driven at 12 Hz PWM via LEDC channel 0.

```
GPIO 25 ──[100Ω]──┬── IRLZ44N Gate
                   │
                  [10kΩ to GND]

IRLZ44N Drain ──── Solenoid (–)
IRLZ44N Source ─── GND

Solenoid (+) ──── +12V (3 A fuse, dedicated run)
1N5822: cathode to +12V, anode to IRLZ44N Drain
```

PI controller adjusts duty based on WBO2 error. Clamped to `ECU_CARB_DUTY_MIN` (10%) – `ECU_CARB_DUTY_MAX` (90%) to prevent no-start conditions.

> `ENABLE_BUZZER` also uses LEDC and will conflict — do not enable it on the ECU node.

### 5.8 Dual Fuel Injectors (Mode 1)

Both injectors fire simultaneously. Each uses the same MOSFET + flyback circuit:

```
GPIO 26 (Inj 1) ──[100Ω]──┬── IRLZ44N Gate
GPIO 27 (Inj 2) ──[100Ω]──┘   (separate MOSFET per injector)
                               [10kΩ] Gate to GND per MOSFET

IRLZ44N Drain ──── Injector (–) ──── Injector (+) ──── +12V (5 A fuse per injector)
IRLZ44N Source ─── GND
1N5822: cathode to +12V, anode to IRLZ44N Drain
```

> **Dedicated injector power:** Route injector +12V directly from the fuse block — do NOT share with the relay controller supply rail. Inductive spikes from injectors will induce noise on shared rails.

### 5.9 Full ECU Node Pin Summary

```
ESP32 #4 (ECU Node)
─────────────────────────────────────────
3.3V ──── 10kΩ pull-ups: GPIO 13 (mode sw), GPIO 32 (CLT), GPIO 33 (IAT), GPIO 34 (RPM)
GND  ──── sensor GND, MOSFET sources, PC817C cathode/emitter
GPIO 4   CAN RX  ←── CAN bus
GPIO 5   CAN TX  ──→ CAN bus
GPIO 13  Mode switch (10kΩ to 3.3V; LOW=carb, HIGH=inject — strapping pin)
GPIO 25  Carb solenoid MOSFET gate  ──[100Ω]── gate; [10kΩ] gate-to-GND
GPIO 26  Injector 1 MOSFET gate     ──[100Ω]── gate; [10kΩ] gate-to-GND
GPIO 27  Injector 2 MOSFET gate     ──[100Ω]── gate; [10kΩ] gate-to-GND
GPIO 32  CLT ←── 10kΩ pull-up to 3.3V + NTC thermistor to GND
GPIO 33  IAT ←── 10kΩ pull-up to 3.3V + NTC thermistor to GND
GPIO 34  RPM ←── PC817C collector (10kΩ pull-up to 3.3V; input-only)
GPIO 35  WBO2 ←── 100kΩ + 100kΩ voltage divider from controller 0–5V (input-only)
GPIO 36  MAP  ←── 100kΩ + 100kΩ voltage divider from MAP sensor 0.2–4.9V (input-only)
GPIO 39  TPS  ←── 100kΩ + 100kΩ voltage divider from throttle pot 0–5V (input-only)
```

---

## 6. Bridge Node (0x05)

The bridge node connects the CAN bus to your home WiFi network. No special sensors or outputs — just an ESP32, a CAN transceiver, and `BRIDGE_MODE` in the config.

### 6.1 CAN Bus

GPIO 5 = TX, GPIO 4 = RX to a CAN transceiver (production mode recommended — the bridge is typically installed permanently alongside the other nodes).

### 6.2 WiFi — STA to Home Router

The bridge joins your home WiFi as a station client (`STA_SSID` / `STA_PASSWORD` in `configs/bridge.h`) while simultaneously running a CAN-local SoftAP. Set these before flashing:

```cpp
#define STA_SSID     "YourHomeNetwork"
#define STA_PASSWORD "YourPassword"
```

No additional hardware is required. The bridge's web UI is reachable from your home network at whatever DHCP address the router assigns — check the serial monitor on first boot for the assigned IP.

### 6.3 MQTT (Optional)

Set `MQTT_BROKER` in `bridge.h` to enable publishing all CAN frames to your broker:

```cpp
#define MQTT_BROKER       "192.168.1.100"   // broker IP or hostname
#define MQTT_TOPIC_PREFIX "canbus"
```

The bridge publishes every frame to `canbus/frames` and subscribes to `canbus/send` for injection. Requires the `PubSubClient` library:

```bash
arduino-cli lib install "PubSubClient"
```

### 6.4 Full Bridge Node Pin Summary

```
ESP32 #5 (Bridge Node)
─────────────────────────────────────────
3V3  ──── Transceiver VCC
GND  ──── Transceiver GND
GPIO 4   CAN RX  ←── Transceiver RXD
GPIO 5   CAN TX  ──→ Transceiver TXD
(no other pins used)
```

---

## 7. Cardputer Node (0x06)

The Cardputer is an M5Stack Cardputer (ESP32-S3 based) used as a portable CAN bus terminal. It has a built-in TFT display, keyboard, and runs in `ESPNOW_ONLY` mode — no SoftAP or web server.

### 7.1 CAN Bus

The Cardputer uses non-default CAN pins since GPIO 4/5 are not available on the M5Stack Cardputer form factor.

| ESP32-S3 Pin | Signal | Goes to |
|--------------|--------|---------|
| GPIO 1 | CAN TX | Transceiver TXD |
| GPIO 2 | CAN RX | Transceiver RXD |

> The config sets `CAN_TX_PIN GPIO_NUM_1` and `CAN_RX_PIN GPIO_NUM_2` to override the defaults.

### 7.2 External CAN Transceiver

The Cardputer has no built-in CAN transceiver. Wire a TJA1051T/3 or SN65HVD230 module to the Grove/external GPIO connector:

```
ESP32-S3 GPIO 1  ──→ Transceiver TXD
ESP32-S3 GPIO 2  ←── Transceiver RXD
ESP32-S3 3.3V    ──→ Transceiver VCC
ESP32-S3 GND     ──→ Transceiver GND
Transceiver CANH ──→ CAN bus
Transceiver CANL ──→ CAN bus
```

### 7.3 ESP-NOW (Wireless Only)

The Cardputer can also operate without a CAN transceiver in ESP-NOW-only mode. It will receive frames from other nodes wirelessly and can inject frames back over ESP-NOW. Set `USE_CAN_TRANSCEIVER 0` if no transceiver is wired (bench mode still initializes TWAI in NO_ACK mode on GPIO 1/2).

### 7.4 Power

The Cardputer has a built-in LiPo battery and USB-C charging. No external power wiring required.

---

## 8. GPS Module

The GPS module provides speed and heading over NMEA via UART. Only the module TX line is needed for basic NMEA reading.

```
GPS module VCC  ──── 3.3V (check module — some require 5V via VIN)
GPS module GND  ──── GND
GPS module TX   ──── GPS_RX_PIN (ESP32 UART RX)
GPS module RX   ──── not connected (optional for config commands)
```

Common modules (u-blox Neo-6M / Neo-8M) default to 9600 baud NMEA output. Set `GPS_BAUD`, `GPS_RX_PIN`, `GPS_TX_PIN`, and `GPS_SERIAL_NUM` in the node config. Speed and heading broadcast on CAN ID `0x305`:

| Bytes | Field | Units |
|-------|-------|-------|
| 0–1 | speed (uint16 LE) | 0.1 mph |
| 2–3 | heading (uint16 LE) | 0.1 degrees true |
| 4 | flags | bit 0 = fix valid, bit 1 = speed valid, bit 2 = heading valid |

---

## 9. CAN Bus Backbone

All nodes connect to the same two-wire CAN bus (CANH / CANL) at **125 kbit/s** with 11-bit IDs.

### 9.1 Production Mode (Transceivers)

Use a TJA1051T/3 or SN65HVD230 on each wired node. Place 120 Ω termination at the two physical ends of the cable.

```
Node A                 Node B                 Node C
[ESP32]──TX→[TJA1051]──CANH──[TJA1051]──CANH──[TJA1051]←TX──[ESP32]
[ESP32]←RX──[TJA1051]──CANL──[TJA1051]──CANL──[TJA1051]─RX→[ESP32]
    └── 120 Ω ──┘                                   └── 120 Ω ──┘
```

**TJA1051T/3 pinout:**

| Pin | Signal | Connect to |
|-----|--------|------------|
| TXD | ESP32 CAN TX | GPIO 5 |
| RXD | ESP32 CAN RX | GPIO 4 |
| VCC | 3V3 | ESP32 3V3 |
| GND | GND | Common GND |
| CANH | Bus high | Twisted pair CANH |
| CANL | Bus low | Twisted pair CANL |
| STB / EN | Standby | Tie to GND (always active) |

**SN65HVD230 pinout:**

| Pin | Signal | Connect to |
|-----|--------|------------|
| D (TXD) | ESP32 CAN TX | GPIO 5 |
| R (RXD) | ESP32 CAN RX | GPIO 4 |
| Vcc | 3.3 V | ESP32 3V3 |
| GND | GND | Common GND |
| CANH | Bus high | Twisted pair CANH |
| CANL | Bus low | Twisted pair CANL |
| RS | Speed / slope | Tie to GND (high-speed mode) |

### 9.2 Bench Mode (No Transceivers)

For bench testing with short runs (< 1 m). GPIO 5 of each node is reconfigured as open-drain via the `GPIO.pin[5].pad_driver` register, and TWAI runs in `TWAI_MODE_NO_ACK`.

```
All nodes GPIO 5 (CAN TX) ──┬──── [1 kΩ–4.7 kΩ pull-up] ──── 3V3
                             └──── All nodes GPIO 4 (CAN RX)
```

> All nodes must have `#define USE_CAN_TRANSCEIVER 0` (or all `1`). A mismatch corrupts the bus.

---

## 10. Power Distribution

### 10.1 Overview

The system uses two isolated 12V sources carried over Cat5e ethernet cable between the relay box and the switch panel. All grounds are joined; the two 12V positives are **never connected to each other**.

| Pair | Color | Signal |
|------|-------|--------|
| Pair 1 | Orange / White-Orange | CAN bus (CANH / CANL) |
| Pair 2 | Blue / White-Blue | Factory battery 12V (+) / GND return |
| Pair 3 | Brown / White-Brown | Accessory battery 12V (+) / GND return |
| Pair 4 | Green / White-Green | 5V logic rail (+) / GND return |

> **Critical:** The Blue (factory) and Brown (accessory) 12V positives must never be bridged. These are two separate batteries; joining the positives without proper isolation circuitry will cause cross-charging and potential damage.

### 10.2 12V Sources

**Factory battery (Blue pair)** — originates at the relay controller node. The relay box connects directly to the factory battery through its own fuse. The relay controller also monitors this rail via its primary battery ADC (GPIO 34). The Blue pair carries monitoring current to the switch panel for 5V redundancy — not relay coil current.

**Accessory battery (Brown pair)** — originates at the switch panel. The switch panel connects to its own dedicated accessory battery and distributes it over the Brown pair back toward the relay box. The relay controller monitors this rail via its auxiliary battery ADC (GPIO 36). Both battery voltages are broadcast on CAN ID 0x300 (TELEMETRY) by whichever node has `ENABLE_BATTERY` configured (currently the switch panel).

### 10.3 5V Logic Rail (Green Pair) — Redundant

Both the relay controller and the switch panel have their own 12V → 5V buck converter, both tied into the Green pair. This gives redundant 5V for the ESP32 logic rail: if one converter fails, the other keeps all nodes running.

```
Relay Controller Node
   [Factory Battery 12V] ──→ [Buck Converter A] ──→ [5V Green+]
                                                        │
Switch Panel Node                                  (shared 5V bus)
   [Accessory Battery 12V] → [Buck Converter B] ──→ [5V Green+]
                                                        │
                                               All ESP32 nodes VIN
```

> **TODO:** Add Schottky ORing diodes (one per buck converter, before they join the Green pair). A voltage mismatch between converters causes the higher-voltage one to carry all load; a failed-shorted converter drags the whole rail down without diodes. With ORing diodes, each converter can fail independently without affecting the other. Each converter must be rated for the full 5V load on its own — do not assume 50/50 load sharing.

### 10.4 Ground

All grounds are joined at a common point. The GND wire in each ethernet pair carries the return current for that pair's signal only — the twisted geometry reduces loop area and EMI. The CAN transceiver ground, ESP32 GND, and all buck converter GNDs tie together at each node.

### 10.5 Relay Load Circuits

Relay output loads (headlights, horn, etc.) are fused and wired directly at the relay box — they do not traverse the ethernet cable. The relay box fuse block sits between the factory battery and the relay output wiring.

```
[Factory Battery +12V]
        │
   [Main Fuse]
        │
   [Relay Box Fuse Block]
   ├── F1 ── Relay 1 load
   ├── F2 ── Relay 2 load
   ├── F3 ── Relay 3 load
   ├── F4 ── Relay 4 load
   ├── F5 ── Relay 5 load (horn — 30 s safety cutoff in firmware)
   └── F6 ── Relay 6 load
```

ECU injectors require separate fused +12V runs directly from the fuse block — do not share with relay load circuits.

---

## 11. Bench Mode vs Production Mode

| | Bench Mode | Production Mode |
|---|---|---|
| `USE_CAN_TRANSCEIVER` | `0` | `1` |
| Transceivers | Not needed | TJA1051T/3 or SN65HVD230 on each node |
| GPIO 5 | Reconfigured to open-drain via `pad_driver` register | Normal push-pull to transceiver TXD |
| Physical wiring | All GPIO 5 + GPIO 4 on a single shared wire + pull-up | Twisted pair CANH/CANL, 120 Ω at each end |
| Max cable length | ~1 m | ~40 m at 125 kbit/s |
| Termination | 1–4.7 kΩ pull-up to 3V3 | 120 Ω at each physical end |

> **Do not use `gpio_set_direction()` to make GPIO 5 open-drain** — it disconnects the TWAI peripheral's signal routing through the GPIO matrix. The `pad_driver` register bit changes only the output driver mode without breaking the peripheral binding.
