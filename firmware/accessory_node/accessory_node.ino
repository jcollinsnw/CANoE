// accessory_node.ino — unified ESP32 firmware for the accessory CAN bus system.
//
// Which features compile in is determined entirely by node_config.h, which is
// copied from firmware/configs/<node>.h by the Makefile before each build.
//
// Init order (mandatory): relay_setup → setup_can → webui_init → bus_init → lcd_setup → menu_setup
//   webui_init brings WiFi up so esp_now_init has a radio to bind to.
//   bus_init requires WiFi already running. menu_setup reads NVS so must run after bus_init.
//
// Supported feature flags (define in node_config.h):
//   ENABLE_RELAY    — relay GPIO control, watchdog, battery telemetry
//   ENABLE_SWITCHES — switch/button inputs, debounce, action dispatch, rotary encoder
//   ENABLE_LCD      — HD44780 16x2 via PCF8574 I2C backpack
//   ENABLE_MENU     — LCD menu system; requires ENABLE_LCD (items gated by MENU_HAS_*)
//   ENABLE_VIPER    — Viper 5305V serial bridge over UART2
//   ENABLE_MPU6050  — MPU-6050 accelerometer / shake detection
//   ENABLE_DHT22    — AM2302 temperature/humidity sensor

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include "driver/twai.h"
#include "soc/gpio_struct.h"

#include "node_config.h"
#include "can_protocol.h"
#include "bus.h"
#include "node_state.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#include "mod_relay.h"
#include "mod_lcd.h"
#include "mod_switches.h"
#include "mod_menu.h"
#include "mod_viper.h"
#include "mod_mpu6050.h"
#include "mod_dht22.h"

// --------------------------------------------------------------
// Shared state definitions (declared extern in node_state.h)
// --------------------------------------------------------------
uint8_t g_relay_mirror = 0;
bool    g_can_ok       = true;
bool    g_menu_active  = false;

// --------------------------------------------------------------
// CAN / TWAI setup (same wiring on all nodes: TX=GPIO5, RX=GPIO4)
// --------------------------------------------------------------
static void setup_can() {
#if USE_CAN_TRANSCEIVER
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_5, GPIO_NUM_4, TWAI_MODE_NORMAL);
#else
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_5, GPIO_NUM_4, TWAI_MODE_NO_ACK);
#endif
  g.tx_queue_len = 10; g.rx_queue_len = 20;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) {
    wlogln("[CAN] init failed"); while (true) delay(1000);
  }
#if !USE_CAN_TRANSCEIVER
  // Open-drain via pad register preserves TWAI signal routing through GPIO matrix.
  // Do NOT use gpio_set_direction() — it would disconnect the TWAI peripheral binding.
  GPIO.pin[5].pad_driver = 1;
  gpio_set_pull_mode(GPIO_NUM_5, GPIO_PULLUP_ONLY);
  wlogln("[CAN] BENCH mode (open-drain TX, NO_ACK)");
#else
  wlogln("[CAN] TRANSCEIVER mode");
#endif
}

// --------------------------------------------------------------
// Arduino lifecycle
// --------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  wlogln("");
  wlogln("=== " NODE_NAME " boot ===");

  // Relay pins must be initialized first so outputs are in a known state
  // before any other setup that might send CAN frames.
#ifdef ENABLE_RELAY
  relay_setup();
#endif

  setup_can();

#if USE_WIFI
  {
    Preferences p; p.begin(NVS_NAMESPACE, true);
    bool wifi_en = p.getBool("wifi_en", true);
    p.end();
    if (wifi_en) {
      webui_init(NODE_NAME, NODE_ID);
      bus_init(NODE_ID);
    } else {
      wlogln("[boot] WiFi disabled by NVS flag");
      bus_init_no_wifi(NODE_ID);
    }
  }
#else
  bus_init_no_wifi(NODE_ID);
#endif

// I2C — initialize once for all modules that share the bus (LCD, MPU-6050).
#if defined(ENABLE_LCD)
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
#elif defined(ENABLE_MPU6050)
  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);
#endif

#ifdef ENABLE_LCD
  lcd_setup();
#endif
#ifdef ENABLE_MENU
  menu_setup();
#endif
#ifdef ENABLE_SWITCHES
  switches_setup();
#endif
#ifdef ENABLE_VIPER
  viper_setup();
#endif
#ifdef ENABLE_MPU6050
  mpu_setup();
#endif
#ifdef ENABLE_DHT22
  dht22_setup();
#endif

#ifdef ENABLE_LCD
  lcd_update_status();
  lcd_set_event(NODE_NAME);
#endif

  wlogln("[boot] ready");
}

void loop() {
#if USE_WIFI
  webui_tick();
#endif
  bus_tick();

  // Per-module periodic work
#ifdef ENABLE_SWITCHES
  switches_loop();
#endif
#ifdef ENABLE_MENU
  menu_tick();
#endif
#ifdef ENABLE_VIPER
  viper_loop();
#endif
#ifdef ENABLE_MPU6050
  mpu_loop();
#endif
#ifdef ENABLE_DHT22
  dht22_loop();
#endif
#ifdef ENABLE_RELAY
  relay_loop();
#endif

  // CAN frame dispatch — update shared state then route to modules
  BusFrame f;
  while (bus_rx(f)) {
    // Maintain the shared relay mirror for any node that observes relay state
    if (f.id == CAN_ID_RELAY_STATUS && f.dlc >= 1) {
      g_relay_mirror = f.data[0];
#ifdef ENABLE_LCD
      lcd_update_status();
#endif
    } else if (f.id == CAN_ID_RELAY_CMD && f.dlc >= 2) {
      uint8_t mask = f.data[0], state = f.data[1];
      g_relay_mirror = (g_relay_mirror & ~mask) | (state & mask);
#ifdef ENABLE_LCD
      char msg[17];
      if (__builtin_popcount(mask) == 1) {
        uint8_t idx = __builtin_ctz(mask);
        snprintf(msg, sizeof(msg), "Relay %u %s", idx + 1, (state & mask) ? "ON" : "OFF");
      } else if (mask == 0x3F && state == 0) {
        snprintf(msg, sizeof(msg), "All OFF");
      } else {
        snprintf(msg, sizeof(msg), "Relays %02X:%02X", mask, state);
      }
      lcd_set_event(msg);
      lcd_update_status();
#endif
    }

    // Route to module handlers
#ifdef ENABLE_RELAY
    relay_handle_frame(f);
#endif
#ifdef ENABLE_SWITCHES
    switches_handle_frame(f);
#endif
#ifdef ENABLE_LCD
    lcd_handle_frame(f);
#endif
#ifdef ENABLE_VIPER
    viper_handle_frame(f);
#endif
  }

  // TWAI health check + bus-off recovery every 2 s
  static uint32_t last_can_check = 0;
  uint32_t now = millis();
  if (now - last_can_check >= 2000) {
    last_can_check = now;
    twai_status_info_t info;
    if (twai_get_status_info(&info) == ESP_OK) {
      bool was_ok = g_can_ok;
      g_can_ok = (info.state == TWAI_STATE_RUNNING);
      wlog("[twai] state=%d tx_err=%u rx_err=%u tx_failed=%u rx_missed=%u\n",
           info.state, info.tx_error_counter, info.rx_error_counter,
           info.tx_failed_count, info.rx_missed_count);
      if (info.state == TWAI_STATE_BUS_OFF) {
        wlogln("[bus] TWAI BUS_OFF — recovering");
        twai_initiate_recovery();
      }
#ifdef ENABLE_LCD
      if (g_can_ok != was_ok) lcd_update_status();
#endif
    }
  }

  // Brief yield for switch panel input polling responsiveness
#ifdef ENABLE_SWITCHES
  delay(1);
#endif
}
