# Adding a New Node — Developer Guide

This guide walks through creating firmware for a new ESP32 on the accessory CAN bus. You don't touch the sketch — everything is controlled by a config header.

---

## How the build system works

The entire firmware lives in one Arduino sketch: `firmware/accessory_node/`. Every feature is wrapped in `#ifdef ENABLE_*` guards, so unneeded modules compile to nothing. Before each build, the Makefile copies the node's config header to `firmware/accessory_node/node_config.h`, which the sketch includes unconditionally.

```
firmware/configs/your_node.h  →  (make copies it)  →  firmware/accessory_node/node_config.h
```

`node_config.h` is intentionally not committed with real content — it's overwritten every build. Always edit `firmware/configs/your_node.h`, never `node_config.h` directly.

---

## Step 1 — Create the config header

Create `firmware/configs/your_node.h`. Start with the required identity block:

```cpp
#pragma once

#define NODE_NAME           "your-node"   // appears in boot log, web UI, LCD
#define NODE_ID             0x04          // must be unique on the bus; 0x01–0x03 are taken
#define USE_CAN_TRANSCEIVER 0             // 0 = bench mode, 1 = production transceivers
#define USE_WIFI            1             // 1 = SoftAP + ESP-NOW + web console
#define NVS_NAMESPACE       "yournode"    // NVS key namespace — max 15 chars, unique per node
```

**NODE_ID** is an 11-bit CAN source identifier. Keep them unique across all physical nodes.

**NVS_NAMESPACE** is the Preferences namespace for this node's persistent storage. Keep it short and unique — collisions between nodes on the same flash would corrupt config data (though in practice each node is a separate physical ESP32).

**USE_CAN_TRANSCEIVER** must match every other node on the bus. In bench mode all nodes wire GPIO 5 (TX) and GPIO 4 (RX) together on a single wire with a pull-up resistor. In production mode each node connects to a TJA1051T/3 or SN65HVD230 transceiver on a twisted-pair CANH/CANL run.

---

## Step 2 — Enable features

Add `#define ENABLE_*` flags for the capabilities this node needs. Every flag is optional — omit it and the module compiles to nothing.

| Flag | What it adds | Required companion defines |
|------|-------------|--------------------------|
| `ENABLE_RELAY` | 6 relay GPIO outputs, safety watchdog, battery ADC telemetry | `NUM_RELAYS`, `RELAY_ACTIVE_HIGH`, `RELAY_PINS_INIT`, `RELAY_MAX_ON_INIT` |
| `ENABLE_SWITCHES` | Switch/button/encoder inputs; publishes `SWITCH_EVENT` / `ENCODER_EVENT` | `NUM_SWITCHES`, `NUM_BUTTONS`, `INPUT_PINS_INIT`, `ENC_CLK_PIN`, `ENC_DT_PIN` |
| `ENABLE_RULES` | CAN-frame-triggered rules engine stored in NVS | `MAX_RULES`, optionally `RULES_DEFAULT_INIT` |
| `ENABLE_LCD` | HD44780 16×2 via PCF8574 I2C backpack | `LCD_I2C_ADDR`, `LCD_SDA_PIN`, `LCD_SCL_PIN` |
| `ENABLE_MENU` | LCD menu system (requires `ENABLE_LCD`) | `MENU_HAS_*` subflags; see below |
| `ENABLE_BUZZER` | Passive piezo tone sequencer | `BUZZER_PIN` |
| `ENABLE_LEDS` | CAN-controllable status LEDs | `NUM_LEDS`, `LED_PINS_INIT`, `LED_ACTIVE_HIGH` |
| `ENABLE_VIPER` | Viper 5305V serial bridge over UART2 | `VIPER_TX_PIN`, `VIPER_RX_PIN` |
| `ENABLE_MPU6050` | MPU-6050 accelerometer / shake detection | `MPU_SDA_PIN`, `MPU_SCL_PIN` (shares I2C with LCD) |
| `ENABLE_DHT22` | AM2302 temperature/humidity broadcast | `DHT22_PIN` |
| `ENABLE_RPM` | Engine RPM via PC817C optocoupler + interrupt counting; optional LCD bar widget | `RPM_PIN` (if sensing); `RPM_WIDGET_ROW` (if LCD widget); `RPM_CYLINDERS` |
| `ENABLE_IGNITION` | Coil + voltage ADC sampler — broadcasts `IGNITION_DATA (0x310)`, used as fuel pump COIL gate | `IGN_COIL_ADC_PIN`, `IGN_COIL_DIVIDER_RATIO`, `IGN_COIL_ON_THRESHOLD_CV`, `IGN_COIL_OFF_THRESHOLD_CV` |
| `ENABLE_FUEL_PUMP_SAFETY` | Multi-gate fuel pump FSM (PRIME → ARMED → RUNNING). Owns a relay (default R1). | `FUEL_PUMP_RELAY`, `FUEL_PUMP_PRIME_MS`, `FUEL_PUMP_RPM_THRESHOLD`, `FUEL_PUMP_STALL_MS`, `FUEL_PUMP_DEFAULT_MODE` |
| `ENABLE_GPS` | GPS speed/heading via NMEA UART; broadcasts `GPS_DATA (0x305)` | `GPS_SERIAL_NUM`, `GPS_RX_PIN`, `GPS_TX_PIN`, `GPS_BAUD` |
| `ENABLE_WBO2` | Wideband O2 analog read; broadcasts `WBO2_DATA (0x306)` | `WBO2_PIN`, `WBO2_SAMPLE_MS`, `WBO2_MIN_V`, `WBO2_MAX_V`, `WBO2_MIN_AFR`, `WBO2_MAX_AFR` |
| `ENABLE_ECU` | Dual-mode fuel controller: carb PI loop or TBI dual-injector; reads MAP/TPS/CLT/IAT | `ECU_*` defines — see ECU node config for full list |
| `BRIDGE_MODE` | CAN ↔ WiFi/STA bridge; no ESP-NOW; aggregated node discovery web UI | `STA_SSID`, `STA_PASSWORD`; optionally `MQTT_BROKER` |

### ENABLE_RELAY

```cpp
#define ENABLE_RELAY

#define NUM_RELAYS          6
#define RELAY_ACTIVE_HIGH   true             // true for ULN2803 (low-side); false for high-side driver
#define RELAY_PINS_INIT     {16, 17, 18, 19, 21, 22}
// Per-relay safety auto-off in ms; 0 = no limit. Index matches relay number (0-based).
#define RELAY_MAX_ON_INIT   {0, 0, 0, 0, 30000, 0}

// Optional — battery voltage ADC
#define VBAT_ADC_PIN        34
#define VBAT_DIVIDER_RATIO  5.545f           // (R_top + R_bot) / R_bot; tune to your resistors
```

### ENABLE_SWITCHES

```cpp
#define ENABLE_SWITCHES

#define NUM_SWITCHES      6                  // first N entries in INPUT_PINS_INIT are latching
#define NUM_BUTTONS       4                  // remaining entries are momentary
#define INPUT_PINS_INIT   {25, 26, 27, 32, 33, 13, 14, 18, 36, 39}
// GPIO 36 and 39 are input-only — no internal pull-up. Wire a 10kΩ resistor to 3V3.

#define ENC_CLK_PIN       34                 // rotary encoder GA / CLK
#define ENC_DT_PIN        35                 // rotary encoder GB / DT
// Note: CJMCU-111 has onboard pull-ups — connect module VCC to 3V3, no external resistors.
```

### ENABLE_RULES

```cpp
#define ENABLE_RULES

#define MAX_RULES 16          // NVS slots reserved for rules (more = more flash)

// Optional compile-time defaults — written to NVS on first boot and after factory reset.
// Omit RULES_DEFAULT_INIT to start with an empty rules table.
#define RULES_DEFAULT_INIT {                                     \
  RULE(TRIG_SW_PRESS(0),   ACT_RELAY_TOGGLE(0)),                \
  RULE(TRIG_SW_PRESS(7),   ACT_ALL_OFF()),                      \
}
```

Available trigger macros: `TRIG_SW_PRESS(idx)`, `TRIG_SW_RELEASE(idx)`, `TRIG_SW_LONG(idx)`, `TRIG_RELAY_BIT_ON(n)`, `TRIG_RELAY_BIT_OFF(n)`, `TRIG_RELAY_CMD_ON(n)`, `TRIG_RELAY_CMD_OFF(n)`.

Available action macros: `ACT_RELAY_TOGGLE(r)`, `ACT_RELAY_ON(r)`, `ACT_RELAY_OFF(r)`, `ACT_ALL_OFF()`, `ACT_LED_ON(node,led)`, `ACT_LED_OFF(node,led)`, `ACT_WIFI_ENABLE(node)`, `ACT_WIFI_DISABLE(node)`, `ACT_VIPER(cmd)`, `ACT_MENU_SELECT()`, `ACT_MENU_ENTER()`.

### ENABLE_LCD

```cpp
#define ENABLE_LCD

#define LCD_I2C_ADDR      0x27    // try 0x3F if blank on first boot
#define LCD_SDA_PIN       21
#define LCD_SCL_PIN       22

// Optional per-relay labels and custom CGRAM icons (used by LCD idle display and menu)
#define RELAY_1_LABEL    "Headlights"
#define RELAY_1_ICON_ON  {0b10101, 0b10101, 0b10101, 0b00000, 0b11111, 0b11111, 0b01110, 0b00000}
#define RELAY_1_ICON_OFF {0b00000, 0b00000, 0b00000, 0b11111, 0b10001, 0b11111, 0b01110, 0b00000}
// ... RELAY_2_LABEL, RELAY_3_LABEL, etc.
// HD44780 CGRAM has 8 slots; slot 0 is reserved, 2 are used by CAN/WiFi status icons.
// That leaves 5 slots for relay icons across all relays combined.
```

### ENABLE_MENU

Requires `ENABLE_LCD`. Enable the submenus you want:

```cpp
#define ENABLE_MENU

#define MENU_HAS_RELAYS    // relay toggle submenu
#define MENU_HAS_VIPER     // Viper lock/unlock/start submenu
#define MENU_HAS_BUS       // live TWAI health counters (read-only)
#define MENU_HAS_DISPLAY   // backlight toggle
#define MENU_HAS_WIFI      // per-node WiFi enable/disable
```

### ENABLE_BUZZER

```cpp
#define ENABLE_BUZZER

#define BUZZER_PIN  16    // passive piezo positive leg; negative leg to GND
                          // do NOT use GPIO 34/35/36/39 — input-only pins
```

### ENABLE_LEDS

```cpp
#define ENABLE_LEDS

#define NUM_LEDS        3
#define LED_PINS_INIT   {17, 19, 23}
#define LED_ACTIVE_HIGH true    // GPIO HIGH = LED on; flip for common-anode wiring
```

### ENABLE_VIPER

```cpp
#define ENABLE_VIPER

#define VIPER_TX_PIN  16    // UART2 TX → level shifter → Viper serial RX
#define VIPER_RX_PIN  17    // UART2 RX ← level shifter ← Viper serial TX
// Viper runs at 5V TTL — a bidirectional level shifter is required.
```

### ENABLE_MPU6050

```cpp
#define ENABLE_MPU6050

#define MPU_SDA_PIN   21    // share I2C pins with LCD if both enabled
#define MPU_SCL_PIN   22
```

### ENABLE_DHT22

```cpp
#define ENABLE_DHT22

#define DHT22_PIN     15
```

### ENABLE_RPM

```cpp
#define ENABLE_RPM

#define RPM_PIN           35    // PC817C collector; input-only — external 10kΩ pull-up to 3.3V required
#define RPM_CYLINDERS     8     // coil fires per crankshaft revolution (8 for V8 distributor)
#define RPM_SAMPLE_MS     500   // how often to compute and broadcast ENGINE_DATA (0x304)

// Optional LCD bar widget — omit if no LCD
#define RPM_WIDGET_ROW    1     // which LCD row to draw the bar on
```

> GPIO 34, 35, 36, and 39 are input-only with no internal pull-up. Any of these used as `RPM_PIN` needs an external 10 kΩ resistor to 3.3V.

### ENABLE_GPS

```cpp
#define ENABLE_GPS

#define GPS_SERIAL_NUM    2     // UART number (Serial2 on most ESP32 boards)
#define GPS_RX_PIN        16    // ESP32 UART RX ← GPS TX
#define GPS_TX_PIN        17    // ESP32 UART TX → GPS RX (optional for read-only)
#define GPS_BAUD          9600  // default for u-blox Neo-6M / Neo-8M
```

Speed and heading broadcast on `GPS_DATA (0x305)`. Only RX is needed for basic NMEA parsing.

### ENABLE_WBO2

```cpp
#define ENABLE_WBO2

#define WBO2_PIN          35    // ADC1 input — connect via 2:1 voltage divider from 0–5V controller output
#define WBO2_SAMPLE_MS    500   // broadcast interval for WBO2_DATA (0x306)

// Voltage at the ADC pin (after divider), not the raw controller output:
#define WBO2_MIN_V        0.0f  // volts at ADC pin = minimum AFR
#define WBO2_MAX_V        2.5f  // volts at ADC pin = maximum AFR (2.5V after 2:1 divider from 5V)
#define WBO2_MIN_AFR      10.0f // AFR at WBO2_MIN_V
#define WBO2_MAX_AFR      20.0f // AFR at WBO2_MAX_V
```

> Mapping is linear. `WBO2_MIN_V`/`WBO2_MAX_V` must be the **post-divider** voltage, not the raw 0–5V controller output.

### BRIDGE_MODE

`BRIDGE_MODE` replaces normal feature flags. Do not combine it with `ENABLE_RELAY`, `ENABLE_SWITCHES`, etc.

```cpp
#define BRIDGE_MODE       // CAN ↔ WiFi/STA bridge; disables ESP-NOW

#define NODE_NAME         "bridge"
#define NODE_ID           0x05
#define USE_CAN_TRANSCEIVER 1   // bridge is typically a permanent install
#define NVS_NAMESPACE     "bridge"

// Home WiFi credentials (STA mode)
#define STA_SSID          "YourHomeNetwork"
#define STA_PASSWORD      "YourPassword"

// SoftAP settings (still runs a local AP for CAN-side devices)
#define AP_SSID           "AccessoryBus"
#define AP_PASSWORD       ""
#define AP_HIDDEN         0

// Optional MQTT publishing — omit to disable
// #define MQTT_BROKER       "192.168.1.100"
// #define MQTT_TOPIC_PREFIX "canbus"
```

When `MQTT_BROKER` is defined, install the PubSubClient library before compiling:
```bash
arduino-cli lib install "PubSubClient"
```

---

## Step 3 — WiFi AP settings (if USE_WIFI 1)

All WiFi nodes broadcast the same SSID so a phone roams between them and ESP-NOW peers find each other.

```cpp
#define AP_SSID     "AccessoryBus"   // must match every other WiFi node
#define AP_PASSWORD ""               // empty = open network; >= 8 chars = WPA2
#define AP_HIDDEN   0                // 1 = suppress SSID broadcast (clients must know the name)
```

If you omit these, `webui.cpp` falls back to `"AccessoryBus"`, open, visible — so they're optional for internal/bench nodes but should be explicit in production configs.

---

## Step 4 — Add a Makefile target

Open the `Makefile` and add three stanzas following the existing pattern:

```makefile
# ---- select node config ----
.PHONY: relay_controller switch_panel viper_interface your_node

your_node:
	cp $(CONFIGS)/your_node.h $(SKETCH)/node_config.h
	arduino-cli compile --fqbn $(FQBN) $(SKETCH)

# ---- compile all ----
all: relay_controller switch_panel viper_interface your_node

# ---- upload ----
.PHONY: upload-your_node

upload-your_node: your_node
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) $(SKETCH)
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)
```

---

## Step 5 — Compile and flash

```bash
# Compile only
make your_node

# Find the serial port
ls /dev/cu.usbserial-* /dev/cu.wchusbserial-* /dev/cu.SLAB_USBtoUART 2>/dev/null

# Compile, upload, and open serial monitor
make upload-your_node PORT=/dev/cu.usbserial-XXXX
```

Watch the serial output at 115200 baud. A healthy boot looks like:

```
=== your-node boot ===
[CAN] BENCH mode (open-drain TX, NO_ACK)
[boot] WiFi disabled by NVS flag      ← or [wifi] AP up SSID="AccessoryBus" ...
[boot] ready
```

---

## Pin assignment rules

| Constraint | Detail |
|-----------|--------|
| GPIO 5 / 4 | Always CAN TX / RX — reserved on every node |
| GPIO 0, 2, 12, 15 | Strapping pins — avoid driving them externally at reset |
| GPIO 13 | Borderline strapping pin — usable but watch for boot-time glitches |
| GPIO 34, 35, 36, 39 | Input-only — no internal pull-up, no `OUTPUT` mode. `INPUT_PULLUP` is silently ignored; wire external 10kΩ to 3V3 for any input on these pins |
| GPIO 16 / 17 | UART2 TX/RX on viper_interface; relay outputs on relay_controller. Fine to reuse on a new board that has neither |
| `Wire.begin()` | Call it once in `accessory_node.ino` setup, never inside a module. The Makefile-generated `accessory_node.ino` calls it automatically before `lcd_setup()` / `mpu_setup()` if either feature is enabled |

---

## Common pitfalls

**Blank LCD on first boot.** The PCF8574 backpack ships from some vendors at address `0x3F` instead of `0x27`. Try the other address in `LCD_I2C_ADDR`.

**CGRAM icon limit.** HD44780 has 8 CGRAM slots. Slot 0 is reserved (it's the C null terminator — using it silently truncates `snprintf` output). Two slots are consumed by the shared CAN/WiFi status icons loaded at the end of `lcd_setup()`. That leaves **5 slots** for relay icons. Defining more than 5 `RELAY_n_ICON_ON/OFF` macros silently skips the overflow — no error, just missing icons.

**GPIO 36/39 pull-ups.** `INPUT_PULLUP` is silently ignored on these pins. The sketch will compile and run, but the input will float and trigger randomly without an external 10kΩ resistor to 3V3.

**Empty AP_PASSWORD vs nullptr.** `webui.cpp` treats `AP_PASSWORD ""` as open (passes `nullptr` to `WiFi.softAP()`). Some ESP32 core versions accept `""` correctly, but `nullptr` is more reliable. Don't try to work around this — just leave `AP_PASSWORD ""` for open networks.

**NVS namespace collision.** If two nodes share a namespace string and also share flash (e.g. you're testing two configs on one board), their Preferences keys overwrite each other. Keep namespaces unique.

**Node ID 0.** `NODE_ID 0` is treated as "unset" in some frame filtering logic. Start from `0x01`.

**Bench vs transceiver mismatch.** If one node has `USE_CAN_TRANSCEIVER 0` and another has `1`, the transceiver node drives the bus in push-pull while the other expects open-drain — bus errors on every frame. All nodes on the same physical wire must agree.

---

## Minimal config template

Copy this and fill in the blanks:

```cpp
// firmware/configs/my_new_node.h
#pragma once

// ---- Identity ----
#define NODE_NAME           "my-node"
#define NODE_ID             0x04
#define USE_CAN_TRANSCEIVER 0       // 0 = bench, 1 = production
#define USE_WIFI            1
#define NVS_NAMESPACE       "mynode"

// ---- WiFi (if USE_WIFI 1) ----
#define AP_SSID     "AccessoryBus"
#define AP_PASSWORD ""
#define AP_HIDDEN   0

// ---- Features ----
// Uncomment what this node needs and add the corresponding pin/config defines below.
// #define ENABLE_RELAY
// #define ENABLE_SWITCHES
// #define ENABLE_RULES
// #define ENABLE_LCD
// #define ENABLE_MENU
// #define ENABLE_BUZZER
// #define ENABLE_LEDS
// #define ENABLE_VIPER
// #define ENABLE_MPU6050
// #define ENABLE_DHT22
// #define ENABLE_RPM
// #define ENABLE_GPS
// #define ENABLE_WBO2
// #define ENABLE_ECU
// For a bridge node, replace everything above with just: #define BRIDGE_MODE

// ---- Pin and module config ----
// (add defines for each ENABLE_* flag you uncommented above)
```
