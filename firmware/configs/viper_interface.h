// Node configuration for the Viper alarm interface ESP32.
// Copied to accessory_node/node_config.h by the Makefile before compiling.

#pragma once

#define NODE_NAME           "viper-iface"
#define NODE_ID             0x03
#define USE_CAN_TRANSCEIVER 0
#define CAN_BUS_SPEED       125   // kbps — change all nodes together: 125, 250, or 500
#define USE_WIFI            1
#define NVS_NAMESPACE       "viperiface"

// ---- WiFi AP settings ----
// Must match the other WiFi nodes so ESP-NOW peers and phones roam correctly.
#define AP_SSID     "REDACTED"
#define AP_PASSWORD "REDACTED"
#define AP_HIDDEN   0

// ---- Features enabled on this node ----
#define ENABLE_VIPER      // Viper 5305V serial bridge via UART2 + level shifter
#define ENABLE_LCD        // HD44780 16x2 via PCF8574 I2C backpack
#define ENABLE_MPU6050    // MPU-6050 accelerometer/gyro (shake detection)

// ---- Viper UART2 pins ----
// Requires a 3.3V<->5V level shifter between these pins and the Viper serial header.
#define VIPER_TX_PIN      16
#define VIPER_RX_PIN      17

// ---- I2C bus (shared by LCD and MPU-6050) ----
#define LCD_I2C_ADDR      0x27  // try 0x3F if blank
#define LCD_SDA_PIN       21
#define LCD_SCL_PIN       22
#define MPU_SDA_PIN       21
#define MPU_SCL_PIN       22

// ---- LCD widget positions (row 0: C[x]W[x] + available space for future widgets) ----
#define BUS_CAN_WIDGET_ROW   0
#define BUS_CAN_WIDGET_COL   0
#define BUS_WIFI_WIDGET_ROW  0
#define BUS_WIFI_WIDGET_COL  2
