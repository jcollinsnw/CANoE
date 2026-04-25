// Node configuration for the switch panel ESP32.
// Copied to accessory_node/node_config.h by the Makefile before compiling.

#pragma once

#define NODE_NAME           "switch-panel"
#define NODE_ID             0x01
#define USE_CAN_TRANSCEIVER 0
#define USE_WIFI            1
#define NVS_NAMESPACE       "swpanel"

// ---- WiFi AP settings ----
// AP_SSID must be the same on every node so the phone roams between them.
// AP_PASSWORD must be "" (empty string) for an open network, or >= 8 chars for WPA2.
// AP_HIDDEN 1 suppresses SSID broadcast; clients must know the name to connect.
#define AP_SSID     "AccessoryBus"
#define AP_PASSWORD ""        // change to WPA2 passphrase before field use
#define AP_HIDDEN   0

// ---- Features enabled on this node ----
#define ENABLE_SWITCHES   // 6 latching switches + 4 buttons + encoder
#define ENABLE_LCD        // HD44780 16x2 via PCF8574 I2C backpack
#define ENABLE_MENU       // LCD menu system (requires ENABLE_LCD)
#define ENABLE_BUZZER     // passive piezo on BUZZER_PIN
#define ENABLE_LEDS       // CAN-controllable status LEDs
#define ENABLE_RULES      // CAN-frame-triggered rules engine

// ---- Piezo buzzer ----
#define BUZZER_PIN        16    // passive piezo positive leg; other leg to GND

// ---- Status LEDs ----
#define NUM_LEDS          3
#define LED_PINS_INIT     {17, 19, 23}
#define LED_ACTIVE_HIGH   true  // LED anode to GPIO via resistor, cathode to GND

// ---- Switch / button inputs ----
// First NUM_SWITCHES entries are latching switches; rest are momentary buttons.
// GPIO 36 and 39 are input-only with no internal pull-up — wire a 10kΩ resistor
// from each pin to 3V3 and connect the button between pin and GND.
#define NUM_SWITCHES      6
#define NUM_BUTTONS       4
#define INPUT_PINS_INIT   {25, 26, 27, 32, 33, 13, 14, 18, 36, 39}

// ---- Rotary encoder ----
// CJMCU-111 (EC11-based) with onboard 3.3k pull-ups. Connect module VCC to 3V3.
// SW contact is not wired on this module — use the button assigned SW_ACT_MENU_NAV.
#define ENC_CLK_PIN       34    // GA (CLK/A)
#define ENC_DT_PIN        35    // GB (DT/B)

// ---- I2C LCD ----
#define LCD_I2C_ADDR      0x27  // try 0x3F if blank
#define LCD_SDA_PIN       21
#define LCD_SCL_PIN       22

// ---- LCD widget positions (row 0: C[x]W[x][RRRRRR]    row 1: event text + RPM bar) ----
#define BUS_CAN_WIDGET_ROW   0   // "C[icon]" at col 0
#define BUS_CAN_WIDGET_COL   0
#define BUS_WIFI_WIDGET_ROW  0   // "W[icon]" at col 2
#define BUS_WIFI_WIDGET_COL  2
#define RELAY_WIDGET_ROW     0   // "[RRRRRR]" at col 4
#define RELAY_WIDGET_COL     4
#define RELAY_WIDGET_WIDTH   8   // 2 brackets + RELAY_DISPLAY_COUNT chars
#define RELAY_DISPLAY_COUNT  6   // number of relay chars the relay widget renders

// ---- LCD menu items ----
#define MENU_HAS_RELAYS   // Relays submenu (toggle relay states)
#define MENU_HAS_VIPER    // Viper submenu (lock/unlock/remote start)
#define MENU_HAS_BUS      // Bus Status submenu (TWAI health counters)
#define MENU_HAS_DISPLAY  // Display submenu (backlight toggle)
#define MENU_HAS_WIFI     // WiFi submenu (enable/disable per node)

// ---- Relay labels and custom LCD icons (optional) ----
// Each relay can have a human-readable label and/or custom CGRAM icons for the
// ON and OFF states. Icons are 8-byte HD44780 5×8 bitmaps. The LCD driver
// allocates CGRAM slots 1–7 automatically in relay order (ON before OFF), so
// defining more than 7 total icons across all relays will silently skip the rest.
// (Slot 0 is reserved — it maps to the C null terminator and corrupts string output.)
//
// Omit any of these defines to use the default label ("Relay N") and
// default characters (\xFF block = ON, '-' = OFF).

#define RELAY_1_LABEL    "Headlights"
#define RELAY_1_ICON_ON  {0b10101, 0b10101, 0b10101, 0b00000, 0b11111, 0b11111, 0b01110, 0b00000}
#define RELAY_1_ICON_OFF {0b00000, 0b00000, 0b00000, 0b11111, 0b10001, 0b11111, 0b01110, 0b00000}

// ---- RPM display widget ----
// Subscribes to ENGINE_DATA (0x304) from the relay controller and renders a bar
// on the LCD. No local coil connection needed on this node.
// Redline is settable at runtime: 400 01 40 00 00 <rpm_lo> <rpm_hi> 01
#define ENABLE_RPM
#define RPM_WIDGET_ROW   1     // row 1 = the event row
#define RPM_WIDGET_COL   8     // columns 8–15 (leaves cols 0–7 for event text)
#define RPM_WIDGET_WIDTH 8
#define RPM_REDLINE      6500

// ---- Rules engine ----
// Each rule: when a CAN frame matching the trigger arrives, fire the action.
// Slots beyond this list are zero-initialised (trig_id=0 = disabled).
// Edit and reflash to change defaults; runtime edits via the web UI Rules tab.
//
// Trigger macros: TRIG_SW_PRESS(idx)  TRIG_SW_RELEASE(idx)  TRIG_SW_LONG(idx)
//                 TRIG_RELAY_BIT_ON(n)  TRIG_RELAY_BIT_OFF(n)
//                 TRIG_RELAY_CMD_ON(n)  TRIG_RELAY_CMD_OFF(n)
// Action macros:  ACT_RELAY_TOGGLE(r)  ACT_RELAY_ON(r)  ACT_RELAY_OFF(r)
//                 ACT_ALL_OFF()  ACT_RELAY_SCENE(bitmap)
//                 ACT_LED_ON(node,led)  ACT_LED_OFF(node,led)
//                 ACT_WIFI_ENABLE(node)  ACT_WIFI_DISABLE(node)
//                 ACT_VIPER(cmd)  ACT_MENU_SELECT()  ACT_MENU_ENTER()
#define MAX_RULES 16

#define RULES_DEFAULT_INIT {                                                              \
  /* Latching switches → relay toggles */                                                 \
  RULE(TRIG_SW_PRESS(0),      ACT_RELAY_TOGGLE(0)),  /* SW1 → R1 */                     \
  RULE(TRIG_SW_PRESS(1),      ACT_RELAY_TOGGLE(1)),  /* SW2 → R2 */                     \
  RULE(TRIG_SW_PRESS(2),      ACT_RELAY_TOGGLE(2)),  /* SW3 → R3 */                     \
  RULE(TRIG_SW_PRESS(3),      ACT_RELAY_TOGGLE(3)),  /* SW4 → R4 */                     \
  /* SW5: hold-style (horn) — relay on while switch held, off on release */               \
  RULE(TRIG_SW_PRESS(4),      ACT_RELAY_ON(4)),      /* SW5 press   → R5 on  */         \
  RULE(TRIG_SW_RELEASE(4),    ACT_RELAY_OFF(4)),     /* SW5 release → R5 off */         \
  /* SW6: relay follows switch position (relay controller MAX_ON_MS caps run time) */     \
  RULE(TRIG_SW_PRESS(5),      ACT_RELAY_ON(5)),      /* SW6 on  → R6 on  */             \
  RULE(TRIG_SW_RELEASE(5),    ACT_RELAY_OFF(5)),     /* SW6 off → R6 off */             \
  /* BTN1: LCD menu navigation */                                                         \
  RULE(TRIG_SW_PRESS(6),      ACT_MENU_SELECT()),    /* BTN1 short → menu select */     \
  RULE(TRIG_SW_LONG(6),       ACT_MENU_ENTER()),     /* BTN1 long  → menu enter */      \
  /* BTN2: all relays off */                                                              \
  RULE(TRIG_SW_PRESS(7),      ACT_ALL_OFF()),        /* BTN2 → all off */               \
  /* Status LEDs mirror relay 1 state */                                                  \
  RULE(TRIG_RELAY_BIT_ON(0),  ACT_LED_ON(NODE_ID, 0)),  /* R1 on  → LED1 on  */        \
  RULE(TRIG_RELAY_BIT_OFF(0), ACT_LED_OFF(NODE_ID, 0)), /* R1 off → LED1 off */        \
  /* BTN3 and BTN4 publish SW_PRESS only — add rules here to give them actions */        \
}
