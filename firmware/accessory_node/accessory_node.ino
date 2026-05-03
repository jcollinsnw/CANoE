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
#include "mod_buzzer.h"
#include "mod_led.h"
#include "mod_rules.h"
#include "mod_viper.h"
#include "mod_mpu6050.h"
#include "mod_dht22.h"
#include "mod_wbo2.h"
#include "mod_bluetooth.h"
#include "mod_ecu.h"
#include "mod_rpm.h"
#include "mod_gps.h"
#include "mod_serial_shell.h"
#include "mod_mqtt.h"
#include "mod_blob.h"
#include "mod_wifi_creds.h"

// --------------------------------------------------------------
// Shared state definitions (declared extern in node_state.h)
// --------------------------------------------------------------
uint8_t g_relay_mirror = 0;
bool    g_menu_active  = false;

// --------------------------------------------------------------
// Node capability advertisement
// --------------------------------------------------------------
static uint8_t node_caps_byte() {
  uint8_t c = 0;
#ifdef ENABLE_RELAY
  c |= NODE_CAP_RELAY;
#endif
#ifdef ENABLE_SWITCHES
  c |= NODE_CAP_SWITCHES;
#endif
#ifdef ENABLE_VIPER
  c |= NODE_CAP_VIPER;
#endif
#ifdef ENABLE_LEDS
  c |= NODE_CAP_LEDS;
#endif
#ifdef ENABLE_RULES
  c |= NODE_CAP_RULES;
#endif
  return c;
}

static void send_node_cap() {
  uint8_t sw = 0, btn = 0, leds = 0, relays = 0;
#ifdef ENABLE_SWITCHES
  sw = NUM_SWITCHES; btn = NUM_BUTTONS;
#endif
#ifdef ENABLE_LEDS
  leds = NUM_LEDS;
#endif
#ifdef ENABLE_RELAY
  relays = NUM_RELAYS;
#endif
  uint8_t d[6] = { bus_node_id(), node_caps_byte(), sw, btn, leds, relays };
  bus_tx(CAN_ID_NODE_CAP, d, 6);
}

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
  // CAN_BUS_SPEED set in node_config.h — all nodes must agree. Default 125 kbps.
#ifndef CAN_BUS_SPEED
#define CAN_BUS_SPEED 125
#endif
#if CAN_BUS_SPEED == 500
  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
#elif CAN_BUS_SPEED == 250
  twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS();
#else
  twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS();
#endif
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

  // NVS may override the compile-time NODE_ID (via CFG_KEY_NODE_ID).
  // Also read audio prefs here — both are needed before lcd_setup() triggers the animation.
  uint8_t eff_id;
  {
    Preferences p; p.begin(NVS_NAMESPACE, true);
    eff_id = p.getUChar("node_id", NODE_ID);
    // startup sound plays only when startup_snd is true AND beep is not muted.
    lcd_set_startup_sound(p.getBool("startup_snd", true) && !p.getBool("beep_muted", false));
    p.end();
  }
  if (eff_id != NODE_ID) wlog("[boot] node_id overridden: 0x%02X -> 0x%02X\n", NODE_ID, eff_id);

#if USE_WIFI
  wifi_creds_setup();
  { uint8_t pmk[16], lmk[16]; wifi_creds_get_pmk(pmk); wifi_creds_get_lmk(lmk); bus_set_espnow_keys(pmk, lmk); }
#endif

#if USE_WIFI
#ifdef BRIDGE_MODE
  // Bridge: AP + web UI always on; no ESP-NOW (avoids channel conflict with STA).
  webui_init(NODE_NAME, eff_id);
  webui_set_ap_client_cb([](uint8_t n, uint8_t old) {
    if (n > old) { lcd_set_event("Web UI connected");    buzzer_wifi_connect(); }
    else         { lcd_set_event("Web UI disconnected"); buzzer_wifi_disconnect(); }
  });
  bus_init_no_wifi(eff_id);
  wlogln("[boot] bridge mode: AP up, ESP-NOW disabled");
#else
  {
    Preferences p; p.begin(NVS_NAMESPACE, true);
    bool ap_en, espnow_en;
    if (p.isKey("ap_en") || p.isKey("espnow_en")) {
      ap_en     = p.getBool("ap_en",     true);
      espnow_en = p.getBool("espnow_en", true);
    } else {
      // Migrate from legacy wifi_en flag (sets both).
      bool wifi_en = p.getBool("wifi_en", true);
      ap_en = espnow_en = wifi_en;
    }
    p.end();

    if (ap_en && espnow_en) {
      webui_init(NODE_NAME, eff_id);
      webui_set_ap_client_cb([](uint8_t n, uint8_t old) {
        if (n > old) { lcd_set_event("Web UI connected");    buzzer_wifi_connect(); }
        else         { lcd_set_event("Web UI disconnected"); buzzer_wifi_disconnect(); }
      });
      bus_init(eff_id);
    } else if (ap_en) {
      // AP + web UI up, but ESP-NOW radio disabled.
      webui_init(NODE_NAME, eff_id);
      webui_set_ap_client_cb([](uint8_t n, uint8_t old) {
        if (n > old) { lcd_set_event("Web UI connected");    buzzer_wifi_connect(); }
        else         { lcd_set_event("Web UI disconnected"); buzzer_wifi_disconnect(); }
      });
      bus_init_no_wifi(eff_id);
      wlogln("[boot] ESP-NOW disabled by config");
    } else if (espnow_en) {
      // ESP-NOW only — WiFi in STA mode on ch6, no AP, no web server.
      bus_init_no_ap(eff_id);
      wlogln("[boot] AP disabled, ESP-NOW only");
    } else {
      // WiFi radio completely off.
      wlogln("[boot] WiFi radio disabled by config");
      bus_init_no_wifi(eff_id);
    }
  }
#endif // BRIDGE_MODE
#else
  bus_init_no_wifi(eff_id);
#endif

  blob_set_commit_cb([](uint8_t ns, uint8_t key, const uint8_t* data, uint16_t len, uint8_t flags) {
#if USE_WIFI
    if (ns == BLOB_NS_WIFI) { wifi_creds_on_blob(key, data, len, flags); return; }
#endif
    // Future namespaces handled here
    wlog("[blob] unhandled ns=0x%02X key=0x%02X len=%u\n", ns, key, len);
  });

// I2C — initialize once for all modules that share the bus (LCD, MPU-6050).
#if defined(ENABLE_LCD)
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
#elif defined(ENABLE_MPU6050)
  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);
#endif

#ifdef ENABLE_LCD
  lcd_setup();
  relay_icons_init();       // must run after lcd_setup() so CGRAM is ready
  bus_register_lcd_widgets();
#endif
#ifdef ENABLE_MENU
  menu_setup();
#endif
#ifdef ENABLE_SWITCHES
  switches_setup();
#endif
#ifdef ENABLE_RULES
  rules_setup();
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
#ifdef ENABLE_RPM
  rpm_setup();
#endif
#ifdef ENABLE_GPS
  gps_setup();
#endif
#ifdef ENABLE_BUZZER
  buzzer_setup();
#endif
#ifdef ENABLE_LEDS
  led_setup();
#endif

#ifdef ENABLE_WBO2
  wbo2_setup();
#endif
#ifdef ENABLE_BLUETOOTH
  bluetooth_setup();
#endif
#ifdef ENABLE_ECU
  ecu_setup();
#endif

#ifdef ENABLE_LCD
  lcd_update_status();
  lcd_set_event(NODE_NAME);
#endif

#ifdef MQTT_BROKER
  mqtt_setup();
#endif
  serial_shell_setup();
  { uint8_t b = bus_node_id(); bus_tx(CAN_ID_BOOT_EVENT, &b, 1); }
  send_node_cap();
#ifdef BRIDGE_MODE
  // Ask all nodes to announce their capabilities so bridge web UI populates immediately.
  { uint8_t b = 0xFF; bus_tx(CAN_ID_NODE_CAP_REQ, &b, 1); }
#endif
  wlogln("[boot] ready");

#ifdef ENABLE_BUZZER
  buzzer_startup();
#endif
  lcd_set_event("Ready");
}

void loop() {
#if USE_WIFI
  webui_tick();
#endif
  bus_tick();
  serial_shell_tick();
#ifdef MQTT_BROKER
  mqtt_tick();
#endif

  // Per-module periodic work
#ifdef ENABLE_SWITCHES
  switches_loop();
#endif
#ifdef ENABLE_MENU
  menu_tick();
#endif
#ifdef ENABLE_RULES
  rules_tick();
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
#ifdef ENABLE_RPM
  rpm_loop();
#endif
#ifdef ENABLE_GPS
  gps_loop();
#endif
#ifdef ENABLE_LCD
  lcd_tick();
#endif
#ifdef ENABLE_BUZZER
  buzzer_tick();
#endif
#ifdef ENABLE_LEDS
  led_tick();
#endif

#ifdef ENABLE_WBO2
  wbo2_loop();
#endif
#ifdef ENABLE_BLUETOOTH
  bluetooth_loop();
#endif
#ifdef ENABLE_ECU
  ecu_loop();
#endif

  // CAN frame dispatch — update shared state then route to modules
  BusFrame f;
  while (bus_rx(f)) {
    serial_shell_print(f);
    // Node capability exchange
    if (f.id == CAN_ID_NODE_CAP_REQ && f.dlc >= 1) {
      if (f.data[0] == 0xFF || f.data[0] == bus_node_id()) send_node_cap();
    }
#if USE_WIFI
    if (f.id == CAN_ID_NODE_CAP) webui_handle_node_cap(f);
#endif
#ifdef MQTT_BROKER
    mqtt_handle_frame(f);
#endif
    // Maintain the shared relay mirror for any node that observes relay state
    if (f.id == CAN_ID_RELAY_STATUS && f.dlc >= 1) {
      g_relay_mirror = f.data[0];
    } else if (f.id == CAN_ID_RELAY_CMD && f.dlc >= 2) {
      uint8_t mask = f.data[0], state = f.data[1];
      g_relay_mirror = (g_relay_mirror & ~mask) | (state & mask);
      {
        char msg[17];
        if (mask == 0x3F && state == 0) {
          lcd_set_event("All OFF");
        } else {
          for (uint8_t i = 0; i < 6; i++) {
            if (mask & (1u << i)) {
              snprintf(msg, sizeof(msg), "%.10s %s",
                       lcd_relay_label(i), (state & (1u << i)) ? "ON" : "OFF");
              lcd_set_event(msg);
              break;
            }
          }
        }
      }
    }

    // WiFi / AP / ESP-NOW enable-disable — handled on every node that has WiFi.
#if USE_WIFI
    if (f.id == CAN_ID_CONFIG_WRITE && f.dlc >= 5 &&
        (f.data[0] == bus_node_id() || f.data[0] == CFG_TARGET_BROADCAST)) {
      uint8_t key = f.data[1];
      if (key == CFG_KEY_NODE_ID && f.data[0] == bus_node_id()) {
        // Broadcast not allowed — reassigning all nodes to the same ID is chaos.
        uint8_t new_id = f.data[4];
        if (new_id >= 0x01 && new_id <= 0xFE) {
          Preferences p; p.begin(NVS_NAMESPACE, false);
          p.putUChar("node_id", new_id);
          p.end();
          wlog("[cfg] node_id 0x%02X -> 0x%02X -> restart\n", bus_node_id(), new_id);
          delay(100); ESP.restart();
        }
      } else if (key == CFG_KEY_WIFI_ENABLED ||
          key == CFG_KEY_AP_ENABLED   ||
          key == CFG_KEY_ESPNOW_ENABLED) {
        bool en = (f.data[4] != 0);
        Preferences p; p.begin(NVS_NAMESPACE, false);
        if (key == CFG_KEY_WIFI_ENABLED) {
          p.putBool("ap_en",     en);   // legacy key: sets both
          p.putBool("espnow_en", en);
        } else if (key == CFG_KEY_AP_ENABLED) {
          p.putBool("ap_en", en);
        } else {
          p.putBool("espnow_en", en);
        }
        p.end();
        wlog("[cfg] key=0x%02X en=%u -> restart\n", key, en);
        delay(100); ESP.restart();
      }
    }
#endif

    // Any node that receives a SWITCH_EVENT from another node sends an ACK so the
    // switch panel can confirm delivery and stop retrying.
    if (f.id == CAN_ID_SWITCH_EVENT && strcmp(f.source, "self") != 0 && f.dlc >= 2) {
      uint8_t ack[2] = { f.data[0], f.data[1] };
      bus_tx(CAN_ID_SWITCH_ACK, ack, 2);
    }

    // Clear pending ACK retry on this node (switch panel only; no-op stub elsewhere).
    switches_handle_ack(f);

    blob_handle_frame(f);

    // Reboot command — restart if this frame targets us or is a broadcast.
    if (f.id == CAN_ID_REBOOT_CMD && f.dlc >= 1) {
      if (f.data[0] == bus_node_id() || f.data[0] == 0xFF) {
        wlog("[boot] reboot cmd target=0x%02X\n", f.data[0]);
        delay(100);
        ESP.restart();
      }
    }

    // Encoder scroll drives menu when active (encoder events are self-echoed from mod_switches).
#if defined(ENABLE_MENU) && defined(ENABLE_SWITCHES)
    if (f.id == CAN_ID_ENCODER_EVENT && f.dlc >= 2 && menu_is_active()) {
      uint8_t count = f.data[1] ? f.data[1] : 1;
      int8_t  dir   = (f.data[0] == ENC_ROTATE_CW) ? 1 : -1;
      for (uint8_t i = 0; i < count; i++) menu_scroll(dir);
    }
#endif

    // Route to module handlers
#ifdef ENABLE_RELAY
    relay_handle_frame(f);
#endif
#ifdef ENABLE_RULES
    rules_handle_frame(f);
#endif
#ifdef ENABLE_LCD
    lcd_handle_frame(f);
#endif
    buzzer_handle_frame(f);
#ifdef ENABLE_VIPER
    viper_handle_frame(f);
#endif

#ifdef ENABLE_LEDS
    led_handle_frame(f);
#endif
#ifdef ENABLE_RPM
    rpm_handle_frame(f);
#endif
#ifdef ENABLE_WBO2
    wbo2_handle_frame(f);
#endif
#ifdef ENABLE_BLUETOOTH
    bluetooth_handle_frame(f);
#endif
#ifdef ENABLE_ECU
    ecu_handle_frame(f);
#endif
  }

  // TWAI health check + bus-off recovery every 2 s
  static uint32_t last_can_check = 0;
  uint32_t now = millis();
  if (now - last_can_check >= 2000) {
    last_can_check = now;
    if (bus_twai_check()) {
      bool can_up = bus_twai_running();
#ifdef ENABLE_LCD
      lcd_update_status();
#endif
      lcd_set_event(can_up ? "CAN ok" : "CAN down");
#ifdef ENABLE_BUZZER
      if (can_up) buzzer_can_up();
      else        buzzer_can_down();
#endif
    }
  }

  // ESP-NOW peer presence changes — show which named nodes came/went.
  static uint8_t last_peer_bitmap = 0;
  {
    uint8_t bm = bus_peer_node_bitmap();
    if (bm != last_peer_bitmap) {
      uint8_t pc      = __builtin_popcount(bm);
      uint8_t changed = bm ^ last_peer_bitmap;
      last_peer_bitmap = bm;
#ifdef ENABLE_LCD
      lcd_update_status();
#endif
      {
        // Short display names indexed by node_id (0x01–0x04)
        static const char* const PEER_NAME[] = { "", "sw", "relay", "viper", "ecu" };
        char msg[17] = {};
        if (bm == 0) {
          strncpy(msg, "No peers", sizeof(msg) - 1);
        } else if (__builtin_popcount(changed) == 1) {
          // Single node changed — name it and say online/offline
          uint8_t bit = __builtin_ctz(changed);
          bool came_on = !!(bm & (1u << bit));
          const char* nm = (bit >= 1 && bit <= 4) ? PEER_NAME[bit] : "node";
          snprintf(msg, sizeof(msg), "%s %s", nm, came_on ? "online" : "offline");
        } else {
          // Multiple changes — list count then all active nodes
          int pos = snprintf(msg, sizeof(msg), "%u:", pc);
          for (uint8_t i = 1; i <= 4 && pos < 16; i++) {
            if (bm & (1u << i))
              pos += snprintf(msg + pos, sizeof(msg) - pos, " %s", PEER_NAME[i]);
          }
        }
        lcd_set_event(msg);
      }
#ifdef ENABLE_BUZZER
      buzzer_peer_count(pc);
#endif
    }
  }

  // Node announce heartbeat — lets every peer know we're alive.
  // All nodes broadcast on the same CAN ID; data[0] carries the node_id so
  // the web UI can identify the sender without per-node CAN IDs.
  static uint32_t last_announce = 0;
  static uint8_t  cap_div = 0;
  if (now - last_announce >= 5000) {
    last_announce = now;
    uint8_t ann[3] = { bus_node_id(), bus_peer_count(), (uint8_t)(bus_can_healthy() ? 1 : 0) };
    bus_tx(CAN_ID_NODE_ANNOUNCE, ann, 3);
    if (++cap_div >= 6) { cap_div = 0; send_node_cap(); }  // caps every 30 s
  }

  // Brief yield for switch panel input polling responsiveness
#ifdef ENABLE_SWITCHES
  delay(1);
#endif
}
