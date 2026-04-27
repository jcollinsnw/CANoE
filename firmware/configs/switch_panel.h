// Node configuration for the switch panel ESP32.
// Copied to accessory_node/node_config.h by the Makefile before compiling.

#pragma once

#define NODE_NAME           "switch-panel"
#define NODE_ID             0x01
#define USE_CAN_TRANSCEIVER 1
#define CAN_BUS_SPEED       125   // kbps — change all nodes together: 125, 250, or 500
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
#define ENABLE_SWITCHES   // 3 latching switches + 7 buttons + encoder
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
// First NUM_SWITCHES entries are latching (toggle) switches; rest are momentary buttons.
// GPIO 36 and 39 are input-only with no internal pull-up — wire a 10kΩ resistor
// from each pin to 3V3 and connect the button between pin and GND.
// GPIO 13 is a strapping pin; if it misbehaves at boot move it to a spare GPIO.
//
// Index → GPIO → role
//   0  GPIO 25  SW1  Fuel Pump     (latching switch → relay 2 follows)
//   1  GPIO 26  SW2  Carb Choke    (latching switch → relay 3 follows)
//   2  GPIO 27  SW3  spare switch
//   3  GPIO 32  BTN1 Menu          (short = select, long = enter/back)
//   4  GPIO 33  BTN2 Horn          (hold = relay 5 on, release = off)
//   5  GPIO 13  BTN3 Headlights    (press = toggle relay 1)
//   6  GPIO 14  BTN4 All Off       (press = all relays off)
//   7  GPIO 18  BTN5 spare button
//   8  GPIO 36  BTN6 spare button  (ext 10kΩ pull-up to 3V3 required)
//   9  GPIO 39  BTN7 spare button  (ext 10kΩ pull-up to 3V3 required)
#define NUM_SWITCHES      3
#define NUM_BUTTONS       7
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

// ---- LCD widget positions (row 0: C[x]N[x]A[x]  RRRRRR) ----
// C=CAN wired health, N=ESP-NOW peer seen, A=WiFi AP active; 2-char gap; 6 relay chars right side
#define BUS_CAN_WIDGET_ROW    0   // "C[icon]" at col 0-1
#define BUS_CAN_WIDGET_COL    0
#define BUS_ESPNOW_WIDGET_ROW 0   // "N[icon]" at col 2-3
#define BUS_ESPNOW_WIDGET_COL 2
#define BUS_AP_WIDGET_ROW     0   // "A[icon]" at col 4-5
#define BUS_AP_WIDGET_COL     4
#define RELAY_WIDGET_ROW      0   // "RRRRRR" at col 10-15 (no brackets)
#define RELAY_WIDGET_COL      10
#define RELAY_WIDGET_WIDTH    6
#define RELAY_DISPLAY_COUNT   6

// ---- LCD menu items ----
#define MENU_HAS_RELAYS   // Relays submenu (toggle relay states)
#define MENU_HAS_TX_MODE  // CAN Mode submenu (CAN+WiFi / WiFi Only / CAN Only)
#define MENU_HAS_VIPER    // Viper submenu (lock/unlock/remote start)
#define MENU_HAS_BUS      // Bus Status submenu (TWAI health counters)
#define MENU_HAS_DISPLAY  // Display submenu (backlight toggle)
#define MENU_HAS_WIFI     // WiFi submenu (enable/disable per node)
#define MENU_HAS_BEEP     // Beep toggle (mute/unmute buzzer)

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
#define RELAY_2_LABEL    "Fuel Pump"
#define RELAY_3_LABEL    "Choke"
#define RELAY_5_LABEL    "Horn"

// ---- RPM ----
// Frame handler + redline config enabled; LCD bar widget is disabled until hardware is ready.
// To re-enable the bar: add RPM_WIDGET_ROW 0, RPM_WIDGET_COL <col>, RPM_WIDGET_WIDTH <w>.
// #define ENABLE_RPM
#define RPM_REDLINE 6500

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
  /* SW1 (idx 0) — Fuel Pump: relay follows switch position */                           \
  RULE(TRIG_SW_PRESS(0),      ACT_RELAY_ON(1)),      /* SW1 on  → R2 on  */            \
  RULE(TRIG_SW_RELEASE(0),    ACT_RELAY_OFF(1)),     /* SW1 off → R2 off */            \
  /* SW2 (idx 1) — Carb Choke: relay follows switch position */                          \
  RULE(TRIG_SW_PRESS(1),      ACT_RELAY_ON(2)),      /* SW2 on  → R3 on  */            \
  RULE(TRIG_SW_RELEASE(1),    ACT_RELAY_OFF(2)),     /* SW2 off → R3 off */            \
  /* SW3 (idx 2) — spare; add rules here */                                              \
  /* BTN1 (idx 3) — Menu navigation */                                                   \
  RULE(TRIG_SW_PRESS(3),      ACT_MENU_SELECT()),    /* BTN1 short → menu select */    \
  RULE(TRIG_SW_LONG(3),       ACT_MENU_ENTER()),     /* BTN1 long  → menu enter/back */\
  /* BTN2 (idx 4) — Horn: hold-style (relay on while held, off on release) */           \
  RULE(TRIG_SW_PRESS(4),      ACT_RELAY_ON(4)),      /* BTN2 press   → R5 on  */       \
  RULE(TRIG_SW_RELEASE(4),    ACT_RELAY_OFF(4)),     /* BTN2 release → R5 off */       \
  /* BTN3 (idx 5) — Headlights: momentary press toggles relay */                        \
  RULE(TRIG_SW_PRESS(5),      ACT_RELAY_TOGGLE(0)), /* BTN3 → toggle R1 */            \
  /* BTN4 (idx 6) — All Off */                                                           \
  RULE(TRIG_SW_PRESS(6),      ACT_ALL_OFF()),        /* BTN4 → all relays off */       \
  /* Status LED 1 mirrors headlights (relay 1) */                                        \
  RULE(TRIG_RELAY_BIT_ON(0),  ACT_LED_ON(NODE_ID, 0)),  /* R1 on  → LED1 on  */       \
  RULE(TRIG_RELAY_BIT_OFF(0), ACT_LED_OFF(NODE_ID, 0)), /* R1 off → LED1 off */       \
  /* BTN5–BTN7 (idx 7–9) — spare; add rules here */                                     \
}
