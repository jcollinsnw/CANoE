// relay_controller.ino
// ESP32 CAN relay controller with WiFi fallback and web UI.
//
// Bus transport: wired TWAI (CAN) + ESP-NOW simultaneously, via bus.cpp.
// Web UI + captive portal + CAN command-line console: webui.cpp.
// Config over CAN with NVS persistence.
// Per-relay auto-off safety watchdog.
//
// Target: ESP32 (WROOM-32 or similar).
// Uses only built-in Arduino-ESP32 libraries (WiFi, esp_now, WebServer,
// DNSServer, Preferences, driver/twai.h).

#include <Arduino.h>
#include <Preferences.h>
#include "driver/twai.h"
#include "soc/gpio_struct.h"

#include "can_protocol.h"
#include "bus.h"

// ==============================================================
// CONFIG — edit these
// ==============================================================

#define USE_CAN_TRANSCEIVER 0
#define USE_WIFI 0           // 0 = CAN-only, no SoftAP, no web UI, no ESP-NOW

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#define CAN_TX_PIN GPIO_NUM_5
#define CAN_RX_PIN GPIO_NUM_4

#define NUM_RELAYS 6
static const uint8_t RELAY_PINS[NUM_RELAYS] = {16, 17, 18, 19, 21, 22};
static const bool RELAY_ACTIVE_HIGH = true;

#define VBAT_ADC_PIN 34
static const float VBAT_DIVIDER_RATIO = 5.545f;

static const uint32_t STATUS_INTERVAL_MS    = 200;
static const uint32_t TELEMETRY_INTERVAL_MS = 1000;

// Node identity
static const char    NODE_NAME[] = "relay-ctrl";
static const uint8_t NODE_ID     = 0x02;   // 0x02 == relay controller

// Default per-relay safety auto-off (ms). 0 = no limit.
// Relay 5 (idx 4) = horn → 30 s max-on.
static const uint16_t DEFAULT_MAX_ON_MS[NUM_RELAYS] = {0, 0, 0, 0, 30000, 0};

// ==============================================================
// State
// ==============================================================
static uint8_t  g_relay_state = 0;
static uint32_t g_relay_on_at[NUM_RELAYS] = {0, 0, 0, 0, 0, 0};
static uint16_t g_max_on_ms[NUM_RELAYS];
static Preferences g_prefs;

// ==============================================================
// Relay control
// ==============================================================
static void relay_write(uint8_t idx, bool on) {
  if (idx >= NUM_RELAYS) return;
  digitalWrite(RELAY_PINS[idx], (on == RELAY_ACTIVE_HIGH) ? HIGH : LOW);
  if (on) {
    if ((g_relay_state & (1 << idx)) == 0) g_relay_on_at[idx] = millis();
    g_relay_state |= (1 << idx);
  } else {
    g_relay_state &= ~(1 << idx);
  }
}
static void apply_relay_cmd(uint8_t mask, uint8_t desired) {
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    if (mask & (1 << i)) relay_write(i, desired & (1 << i));
  }
}

// ==============================================================
// NVS config
// ==============================================================
static void config_load() {
  g_prefs.begin("relayctl", true);
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    char k[12]; snprintf(k, sizeof(k), "maxon%u", i);
    g_max_on_ms[i] = g_prefs.isKey(k)
      ? (uint16_t)g_prefs.getUShort(k, DEFAULT_MAX_ON_MS[i])
      : DEFAULT_MAX_ON_MS[i];
  }
  g_prefs.end();
}
static void config_save() {
  g_prefs.begin("relayctl", false);
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    char k[12]; snprintf(k, sizeof(k), "maxon%u", i);
    g_prefs.putUShort(k, g_max_on_ms[i]);
  }
  g_prefs.end();
  wlogln("[cfg] saved");
}
static void config_factory_reset() {
  g_prefs.begin("relayctl", false); g_prefs.clear(); g_prefs.end();
  for (uint8_t i = 0; i < NUM_RELAYS; i++) g_max_on_ms[i] = DEFAULT_MAX_ON_MS[i];
  wlogln("[cfg] factory reset");
}

// ==============================================================
// Outgoing frames (now routed through bus_tx)
// ==============================================================
static void send_relay_status() {
  uint8_t data[1] = { g_relay_state };
  bus_tx(CAN_ID_RELAY_STATUS, data, 1);
}
static void send_telemetry() {
#if VBAT_ADC_PIN >= 0
  int raw = analogRead(VBAT_ADC_PIN);
  float vadc = (raw / 4095.0f) * 3.3f;
  int16_t vbat_cv = (int16_t)(vadc * VBAT_DIVIDER_RATIO * 100.0f);
  uint8_t d[8] = {};
  pack_i16(&d[0], vbat_cv);
  bus_tx(CAN_ID_TELEMETRY, d, 8);
#endif
}
static void send_cfg_read_resp(uint8_t idx) {
  if (idx >= NUM_RELAYS) return;
  uint8_t d[8] = {
    CFG_TARGET_RELAY_CTRL, CFG_KEY_RELAY_MAX_ON_MS, idx, 0, 0, 0, 0, 0
  };
  pack_u16(&d[5], g_max_on_ms[idx]);
  bus_tx(CAN_ID_CONFIG_READ_RESP, d, 8);
}

// ==============================================================
// Config-over-CAN handlers
// ==============================================================
static bool cfg_for_us(uint8_t target) {
  return target == CFG_TARGET_RELAY_CTRL || target == CFG_TARGET_BROADCAST;
}
static void handle_cfg_write(const BusFrame& f) {
  if (f.dlc < 8 || !cfg_for_us(f.data[0])) return;
  uint8_t key = f.data[1], idx = f.data[2];
  uint16_t arg2 = unpack_u16(&f.data[5]);
  uint8_t flags = f.data[7];
  if (key == CFG_KEY_RELAY_MAX_ON_MS && idx < NUM_RELAYS) {
    g_max_on_ms[idx] = arg2;
    wlog("[cfg<-] relay %u max_on_ms=%u\n", idx, arg2);
    if (flags & 0x01) config_save();
    send_cfg_read_resp(idx);
  }
  if (key == CFG_KEY_WIFI_ENABLED) {
#if USE_WIFI
    bool en = (f.data[4] != 0);
    g_prefs.begin("relayctl", false);
    g_prefs.putBool("wifi_en", en);
    g_prefs.end();
    wlog("[cfg<-] wifi_en=%u -> restart\n", en);
    delay(100);
    ESP.restart();
#else
    wlogln("[cfg<-] wifi_en ignored (USE_WIFI=0, recompile to enable)");
#endif
  }
}
static void handle_cfg_read(const BusFrame& f) {
  if (f.dlc < 3 || !cfg_for_us(f.data[0])) return;
  uint8_t key = f.data[1], idx = f.data[2];
  if (key != CFG_KEY_RELAY_MAX_ON_MS) return;
  if (idx == 0xFF) {
    for (uint8_t i = 0; i < NUM_RELAYS; i++) { send_cfg_read_resp(i); delay(3); }
  } else if (idx < NUM_RELAYS) {
    send_cfg_read_resp(idx);
  }
}
static void handle_cfg_save(const BusFrame& f) {
  if (f.dlc < 2 || !cfg_for_us(f.data[0])) return;
  switch (f.data[1]) {
    case CFG_SAVE_COMMIT:        config_save(); break;
    case CFG_SAVE_RELOAD:        config_load(); break;
    case CFG_SAVE_FACTORY_RESET: config_factory_reset(); break;
  }
}

// ==============================================================
// Safety watchdog (force-off overdue relays)
// ==============================================================
static void service_watchdog() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    if (!g_max_on_ms[i] || !(g_relay_state & (1 << i))) continue;
    if ((now - g_relay_on_at[i]) >= g_max_on_ms[i]) {
      wlog("[watchdog] relay %u forced OFF after %u ms\n", i, g_max_on_ms[i]);
      relay_write(i, false);
      send_relay_status();
    }
  }
}

// ==============================================================
// CAN setup (wire side)
// ==============================================================
static void setup_can() {
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NO_ACK);
  g.tx_queue_len = 10; g.rx_queue_len = 20;
  twai_timing_config_t t = TWAI_TIMING_CONFIG_125KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK || twai_start() != ESP_OK) {
    wlogln("[CAN] init failed"); while (true) delay(1000);
  }
#if !USE_CAN_TRANSCEIVER
  // Set TX pin to open-drain via register to preserve TWAI signal routing
  GPIO.pin[CAN_TX_PIN].pad_driver = 1;  // 1 = open-drain
  gpio_set_pull_mode(CAN_TX_PIN, GPIO_PULLUP_ONLY);
  wlogln("[CAN] BENCH mode (open-drain TX, NO_ACK)");
#else
  wlogln("[CAN] TRANSCEIVER mode (NO_ACK)");
#endif
}

// ==============================================================
// Arduino lifecycle
// ==============================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  wlogln("");
  wlogln("=== Relay Controller boot ===");

  for (uint8_t i = 0; i < NUM_RELAYS; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    relay_write(i, false);
  }
#if VBAT_ADC_PIN >= 0
  analogReadResolution(12);
#endif

  config_load();

  setup_can();
#if USE_WIFI
  {
    Preferences p; p.begin("relayctl", true);
    bool wifi_en = p.getBool("wifi_en", true);
    p.end();
    if (wifi_en) { webui_init(NODE_NAME, NODE_ID); bus_init(NODE_ID); }
    else          { wlogln("[boot] WiFi disabled by NVS flag"); bus_init_no_wifi(NODE_ID); }
  }
#else
  bus_init_no_wifi(NODE_ID);
#endif

  wlog("[boot] %u relays ready (all OFF)\n", NUM_RELAYS);
  for (uint8_t i = 0; i < NUM_RELAYS; i++)
    wlog("  relay %u: max_on_ms=%u\n", i, g_max_on_ms[i]);
}

void loop() {
#if USE_WIFI
  webui_tick();
#endif
  bus_tick();

  BusFrame f;
  while (bus_rx(f)) {
    switch (f.id) {
      case CAN_ID_RELAY_CMD:
        if (f.dlc >= 2) {
          apply_relay_cmd(f.data[0], f.data[1]);
          wlog("[cmd via %s] mask=0x%02X state=0x%02X -> 0x%02X\n",
                        f.source, f.data[0], f.data[1], g_relay_state);
          send_relay_status();
        }
        break;
      case CAN_ID_CONFIG_WRITE:    handle_cfg_write(f); break;
      case CAN_ID_CONFIG_READ_REQ: handle_cfg_read(f);  break;
      case CAN_ID_CONFIG_SAVE:     handle_cfg_save(f);  break;
      default: break;
    }
  }

  service_watchdog();

  uint32_t now = millis();
  static uint32_t last_status = 0;
  if (now - last_status >= STATUS_INTERVAL_MS) { last_status = now; send_relay_status(); }
  static uint32_t last_telem = 0;
  if (now - last_telem >= TELEMETRY_INTERVAL_MS) { last_telem = now; send_telemetry(); }

  // Occasional bus-off recovery + debug
  static uint32_t last_check = 0;
  if (now - last_check >= 2000) {
    last_check = now;
    twai_status_info_t info;
    if (twai_get_status_info(&info) == ESP_OK) {
      wlog("[twai] state=%d tx_err=%u rx_err=%u tx_failed=%u rx_missed=%u\n",
                    info.state, info.tx_error_counter, info.rx_error_counter,
                    info.tx_failed_count, info.rx_missed_count);
      if (info.state == TWAI_STATE_BUS_OFF) {
        wlogln("[bus] TWAI BUS_OFF — recovering");
        twai_initiate_recovery();
      }
    }
  }
}
