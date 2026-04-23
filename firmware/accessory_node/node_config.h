// Node configuration for the switch panel ESP32.
// Copied to accessory_node/node_config.h by the Makefile before compiling.

#pragma once

#define NODE_NAME           "switch-panel"
#define NODE_ID             0x01
#define USE_CAN_TRANSCEIVER 0
#define USE_WIFI            1
#define NVS_NAMESPACE       "swpanel"

// ---- Features enabled on this node ----
#define ENABLE_SWITCHES   // 6 latching switches + 4 buttons + encoder
#define ENABLE_LCD        // HD44780 16x2 via PCF8574 I2C backpack
#define ENABLE_MENU       // LCD menu system (requires ENABLE_LCD)

// ---- Switch / button inputs ----
// First NUM_SWITCHES entries are latching switches; rest are momentary buttons.
#define NUM_SWITCHES      6
#define NUM_BUTTONS       4
#define INPUT_PINS_INIT   {25, 26, 27, 32, 33, 13, 14, 18, 19, 23}

// ---- Rotary encoder ----
// CJMCU-111 (EC11-based) with onboard 3.3k pull-ups. Connect module VCC to 3V3.
// SW contact is not wired on this module — use the button assigned SW_ACT_MENU_NAV.
#define ENC_CLK_PIN       34    // GA (CLK/A)
#define ENC_DT_PIN        35    // GB (DT/B)

// ---- I2C LCD ----
#define LCD_I2C_ADDR      0x27  // try 0x3F if blank
#define LCD_SDA_PIN       21
#define LCD_SCL_PIN       22

// ---- LCD menu items ----
#define MENU_HAS_RELAYS   // Relays submenu (toggle relay states)
#define MENU_HAS_VIPER    // Viper submenu (lock/unlock/remote start)
#define MENU_HAS_BUS      // Bus Status submenu (TWAI health counters)
#define MENU_HAS_DISPLAY  // Display submenu (backlight toggle)
#define MENU_HAS_WIFI     // WiFi submenu (enable/disable per node)

// ---- Relay labels and custom LCD icons (optional) ----
// Each relay can have a human-readable label and/or custom CGRAM icons for the
// ON and OFF states. Icons are 8-byte HD44780 5×8 bitmaps. The LCD driver
// allocates CGRAM slots 0–7 automatically in relay order (ON before OFF), so
// defining more than 8 total icons across all relays will silently skip the rest.
//
// Omit any of these defines to use the default label ("Relay N") and
// default characters (\xFF block = ON, '-' = OFF).

#define RELAY_1_LABEL    "Headlights"
#define RELAY_1_ICON_ON  {0b01000, 0b00110, 0b10111, 0b00111, 0b10111, 0b00110, 0b01000, 0b00000}
#define RELAY_1_ICON_OFF {0b01000, 0b00110, 0b01011, 0b01011, 0b01011, 0b00110, 0b00010, 0b00000}

// ---- Default switch → relay action map ----
// BTN1 (index 6) is the menu nav button. BTN2 (index 7) turns all relays off.
// Reassignable at runtime via CONFIG_WRITE (CFG_KEY_SW_ACTION).
#define SW_DEFAULT_MAP_INIT {                       \
  {SW_ACT_TOGGLE,     0, 0},    /* SW1 → R1 */     \
  {SW_ACT_TOGGLE,     1, 0},    /* SW2 → R2 */     \
  {SW_ACT_TOGGLE,     2, 0},    /* SW3 → R3 */     \
  {SW_ACT_TOGGLE,     3, 0},    /* SW4 → R4 */     \
  {SW_ACT_HOLD,       4, 0},    /* SW5 → horn */   \
  {SW_ACT_PULSE,      5, 3000}, /* SW6 → 3 s */    \
  {SW_ACT_MENU_NAV,   0, 0},    /* BTN1 → menu */  \
  {SW_ACT_ALL_OFF,    0, 0},    /* BTN2 → all off */\
  {SW_ACT_EVENT_ONLY, 0, 0},    /* BTN3 */         \
  {SW_ACT_EVENT_ONLY, 0, 0},    /* BTN4 */         \
}
