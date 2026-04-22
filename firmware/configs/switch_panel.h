// Node configuration for the switch panel ESP32.
// Copied to accessory_node/node_config.h by the Makefile before compiling.

#pragma once

#define NODE_NAME           "switch-panel"
#define NODE_ID             0x01
#define USE_CAN_TRANSCEIVER 0
#define USE_WIFI            1
#define NVS_NAMESPACE       "swpanel"

// ---- Features enabled on this node ----
#define ENABLE_SWITCHES   // 6 latching switches + 4 buttons + encoder + menu
#define ENABLE_LCD        // HD44780 16x2 via PCF8574 I2C backpack
#define ENABLE_DHT22      // AM2302 temperature/humidity sensor

// ---- Switch / button inputs ----
// First NUM_SWITCHES entries are latching switches; rest are momentary buttons.
#define NUM_SWITCHES      6
#define NUM_BUTTONS       4
#define INPUT_PINS_INIT   {25, 26, 27, 32, 33, 13, 14, 18, 19, 23}

// ---- Rotary encoder ----
// CJMCU-111 (EC11-based) with onboard 3.3k pull-ups. Connect module VCC to 3V3.
// SW contact is not wired on this module — use MENU_BTN_IDX for select/back.
#define ENC_CLK_PIN       34    // GA (CLK/A)
#define ENC_DT_PIN        35    // GB (DT/B)
#define MENU_BTN_IDX      6     // BTN1 = input index 6 (GPIO 14)

// ---- I2C LCD ----
#define LCD_I2C_ADDR      0x27  // try 0x3F if blank
#define LCD_SDA_PIN       21
#define LCD_SCL_PIN       22

// ---- DHT22 ----
// GPIO 0 is a strapping pin; the sensor pull-up keeps it HIGH at idle so
// boot is normally fine. Move to another GPIO if the board won't start.
#define DHT_PIN           0

// ---- Default switch → relay action map ----
#define SW_DEFAULT_MAP_INIT {                  \
  {SW_ACT_TOGGLE,     0, 0},    /* SW1 */      \
  {SW_ACT_TOGGLE,     1, 0},    /* SW2 */      \
  {SW_ACT_TOGGLE,     2, 0},    /* SW3 */      \
  {SW_ACT_TOGGLE,     3, 0},    /* SW4 */      \
  {SW_ACT_HOLD,       4, 0},    /* SW5 horn */  \
  {SW_ACT_PULSE,      5, 3000}, /* SW6 3 s */   \
  {SW_ACT_EVENT_ONLY, 0, 0},    /* BTN1 */      \
  {SW_ACT_EVENT_ONLY, 0, 0},    /* BTN2 */      \
  {SW_ACT_EVENT_ONLY, 0, 0},    /* BTN3 */      \
  {SW_ACT_EVENT_ONLY, 0, 0},    /* BTN4 */      \
}
