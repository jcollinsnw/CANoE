// bus.cpp — see bus.h

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "driver/twai.h"
#include "node_config.h"
#include "bus.h"

#ifdef ENABLE_LCD
#include "mod_lcd.h"
#endif

// --------------------------------------------------------------
// Wire format for ESP-NOW. Magic filters out random broadcasts
// from other projects sharing the airwaves.
// --------------------------------------------------------------
static const uint32_t WIFI_FRAME_MAGIC = 0xCA5BABE7;
struct __attribute__((packed)) WifiFrame {
  uint32_t magic;
  uint8_t  node_id;
  uint16_t seq;
  uint32_t can_id;
  uint8_t  dlc;
  uint8_t  data[8];
};

// --------------------------------------------------------------
// State
// --------------------------------------------------------------
static uint8_t g_node_id = 0;
static uint16_t g_tx_seq = 0;
static BusTxMode g_tx_mode = BUS_TX_CAN_WIFI;
static bool g_wifi_enabled = false;

static uint32_t g_last_can_tx_ok  = 0;
static uint32_t g_last_can_rx     = 0;
static uint32_t g_last_wifi_rx    = 0;
static uint16_t g_tx_fail_streak  = 0;
static bool     g_twai_was_ok     = false;

static bus_observer_t g_observer = nullptr;

// Per-peer activity tracking — used to report connect/disconnect counts.
// Slot node_id==0 means empty. Timeout matches bus_wifi_seen_peer() window.
static const uint32_t PEER_TIMEOUT_MS = 12000; // 2.4× announce interval; node must miss 2 beats
struct PeerEntry { uint8_t node_id; uint32_t last_seen_ms; };
static PeerEntry g_peers[8] = {};

static void peer_touch(uint8_t node_id) {
  uint32_t now = millis();
  for (int i = 0; i < 8; i++) {
    if (g_peers[i].node_id == node_id) { g_peers[i].last_seen_ms = now; return; }
  }
  for (int i = 0; i < 8; i++) {
    if (!g_peers[i].node_id || (now - g_peers[i].last_seen_ms) >= PEER_TIMEOUT_MS) {
      g_peers[i] = {node_id, now}; return;
    }
  }
}

// Dedup cache for (node_id, seq) pairs seen in the last ~2 seconds
struct DedupEntry { uint8_t node_id; uint16_t seq; uint32_t t_ms; };
static const int DEDUP_SIZE = 64;
static DedupEntry g_dedup[DEDUP_SIZE];

static bool dedup_seen_or_record(uint8_t node_id, uint16_t seq) {
  uint32_t now = millis();
  int free_idx = -1;
  for (int i = 0; i < DEDUP_SIZE; i++) {
    if (g_dedup[i].t_ms && (now - g_dedup[i].t_ms) > 2000) g_dedup[i].t_ms = 0;
    if (g_dedup[i].t_ms && g_dedup[i].node_id == node_id && g_dedup[i].seq == seq) return true;
    if (!g_dedup[i].t_ms && free_idx < 0) free_idx = i;
  }
  if (free_idx >= 0) g_dedup[free_idx] = {node_id, seq, now ? now : 1};
  return false;
}

// Ring buffer of received frames (from either transport)
static const int RX_RING_SIZE = 32;
static BusFrame g_ring[RX_RING_SIZE];
static volatile int g_ring_head = 0;
static volatile int g_ring_tail = 0;

static void ring_push(const BusFrame& f) {
  int next = (g_ring_head + 1) % RX_RING_SIZE;
  if (next == g_ring_tail) g_ring_tail = (g_ring_tail + 1) % RX_RING_SIZE; // overwrite oldest
  g_ring[g_ring_head] = f;
  g_ring_head = next;
}

// --------------------------------------------------------------
// ESP-NOW callbacks
// --------------------------------------------------------------
// arduino-esp32 v3.x changed the ESP-NOW receive callback signature.
#if defined(ESP_ARDUINO_VERSION) && ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
static void on_espnow_recv(const esp_now_recv_info_t* /*recv_info*/, const uint8_t* data, int len) {
#else
static void on_espnow_recv(const uint8_t* /*mac*/, const uint8_t* data, int len) {
#endif
  if (len != sizeof(WifiFrame)) return;
  WifiFrame f;
  memcpy(&f, data, sizeof(f));
  if (f.magic != WIFI_FRAME_MAGIC) return;
  if (f.node_id == g_node_id) return; // our own broadcast coming back
  if (dedup_seen_or_record(f.node_id, f.seq)) return;
  g_last_wifi_rx = millis();
  peer_touch(f.node_id);

  BusFrame b = {};
  b.id = f.can_id;
  b.dlc = f.dlc;
  memcpy(b.data, f.data, 8);
  strncpy(b.source, "wifi", sizeof(b.source));
  ring_push(b);
  if (g_observer) g_observer(b, /*outbound=*/false);
}

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------
void bus_init(uint8_t node_id) {
  g_node_id = node_id;
  memset(g_dedup, 0, sizeof(g_dedup));
  g_ring_head = g_ring_tail = 0;

  // WiFi must be initialized (by webui_init) before esp_now_init.
  if (esp_now_init() != ESP_OK) {
    Serial.println("[bus] esp_now_init failed");
    return;
  }
  esp_now_register_recv_cb(on_espnow_recv);

  esp_now_peer_info_t peer = {};
  memset(peer.peer_addr, 0xFF, 6); // broadcast MAC
  peer.channel = 0;                // follow current WiFi channel
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  g_wifi_enabled = true;
  Serial.printf("[bus] ready (wifi+can), node_id=0x%02X\n", node_id);
}

void bus_init_no_ap(uint8_t node_id) {
  g_node_id = node_id;
  memset(g_dedup, 0, sizeof(g_dedup));
  g_ring_head = g_ring_tail = 0;

  // Start WiFi in STA mode so the radio is up for ESP-NOW without launching a SoftAP.
  // Channel must be fixed manually — without an AP association there is no automatic
  // channel negotiation and peers need to agree on the same channel (6).
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[bus] esp_now_init failed (no-ap mode)");
    return;
  }
  esp_now_register_recv_cb(on_espnow_recv);

  esp_now_peer_info_t peer = {};
  memset(peer.peer_addr, 0xFF, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  g_wifi_enabled = true;
  Serial.printf("[bus] ready (espnow, no AP), node_id=0x%02X\n", node_id);
}

void bus_init_no_wifi(uint8_t node_id) {
  g_node_id = node_id;
  g_tx_mode = BUS_TX_CAN_ONLY;
  memset(g_dedup, 0, sizeof(g_dedup));
  g_ring_head = g_ring_tail = 0;
  Serial.printf("[bus] ready (can-only), node_id=0x%02X\n", node_id);
}

bool bus_tx(uint32_t id, const uint8_t* data, uint8_t dlc) {
  bool any_ok = false;

  // Wired CAN
  if (g_tx_mode != BUS_TX_WIFI_ONLY) {
    twai_message_t m = {};
    m.identifier = id;
    m.data_length_code = dlc;
    memcpy(m.data, data, dlc);
    if (twai_transmit(&m, pdMS_TO_TICKS(10)) == ESP_OK) {
      any_ok = true;
      g_last_can_tx_ok = millis();
      g_tx_fail_streak = 0;
    } else {
      if (++g_tx_fail_streak >= 5) {
        twai_clear_transmit_queue();
        g_tx_fail_streak = 0;
        Serial.println("[bus] TX queue flushed (no ACK)");
      }
    }
  }

  // ESP-NOW broadcast
  WifiFrame f;
  f.magic   = WIFI_FRAME_MAGIC;
  f.node_id = g_node_id;
  f.seq     = g_tx_seq++;
  f.can_id  = id;
  f.dlc     = dlc;
  memset(f.data, 0, 8);
  memcpy(f.data, data, dlc);

  dedup_seen_or_record(g_node_id, f.seq); // suppress our own echo if it wraps around

  if (g_wifi_enabled && g_tx_mode != BUS_TX_CAN_ONLY) {
    uint8_t bcast[6]; memset(bcast, 0xFF, 6);
    if (esp_now_send(bcast, (uint8_t*)&f, sizeof(f)) == ESP_OK) any_ok = true;
  }

  // Feed our own outbound frames back into the rx ring so application
  // code (LCD updates, state mirrors) reacts to web-UI-sourced frames
  // the same way it reacts to frames from other nodes.
  {
    BusFrame ob = {};
    ob.id = id; ob.dlc = dlc;
    memcpy(ob.data, data, dlc);
    strncpy(ob.source, "self", sizeof(ob.source));
    ring_push(ob);
    if (g_observer) g_observer(ob, /*outbound=*/true);
  }

  return any_ok;
}

bool bus_rx(BusFrame& out) {
  if (g_ring_tail == g_ring_head) return false;
  out = g_ring[g_ring_tail];
  g_ring_tail = (g_ring_tail + 1) % RX_RING_SIZE;
  return true;
}

void bus_tick() {
  // Drain the TWAI driver into our ring
  twai_message_t rx;
  while (twai_receive(&rx, 0) == ESP_OK) {
    g_last_can_rx = millis();
    BusFrame b = {};
    b.id = rx.identifier;
    b.dlc = rx.data_length_code;
    memcpy(b.data, rx.data, rx.data_length_code);
    strncpy(b.source, "can", sizeof(b.source));
    ring_push(b);
    if (g_observer) g_observer(b, /*outbound=*/false);
  }
}

uint8_t bus_node_id()       { return g_node_id; }
bool bus_can_healthy()      { uint32_t n=millis(); return (n-g_last_can_rx)<5000 || (n-g_last_can_tx_ok)<2000; }

bool bus_twai_check() {
  twai_status_info_t info;
  if (twai_get_status_info(&info) != ESP_OK) return false;
  bool ok = (info.state == TWAI_STATE_RUNNING);
  Serial.printf("[twai] state=%d tx_err=%u rx_err=%u tx_failed=%u rx_missed=%u\n",
                info.state, info.tx_error_counter, info.rx_error_counter,
                info.tx_failed_count, info.rx_missed_count);
  if (info.state == TWAI_STATE_BUS_OFF) {
    Serial.println("[bus] TWAI BUS_OFF — recovering");
    twai_initiate_recovery();
  }
  bool changed = (ok != g_twai_was_ok);
  g_twai_was_ok = ok;
  return changed;
}
bool bus_wifi_seen_peer()   { return g_wifi_enabled && (millis() - g_last_wifi_rx) < 5000; }
bool bus_twai_running()     { return g_twai_was_ok; }

uint8_t bus_peer_count() {
  uint32_t now = millis();
  uint8_t n = 0;
  for (int i = 0; i < 8; i++)
    if (g_peers[i].node_id && (now - g_peers[i].last_seen_ms) < PEER_TIMEOUT_MS) n++;
  return n;
}

uint8_t bus_get_peer_ids(uint8_t* out, uint8_t max) {
  uint32_t now = millis();
  uint8_t n = 0;
  for (int i = 0; i < 8 && n < max; i++) {
    if (g_peers[i].node_id && (now - g_peers[i].last_seen_ms) < PEER_TIMEOUT_MS)
      out[n++] = g_peers[i].node_id;
  }
  return n;
}

// Bitmask of active ESP-NOW peer node IDs seen within PEER_TIMEOUT_MS.
// Bit N is set if node 0xN has been heard from recently (nodes are 0x01-0x04).
uint8_t bus_peer_node_bitmap() {
  uint32_t now = millis();
  uint8_t bits = 0;
  for (int i = 0; i < 8; i++) {
    uint8_t nid = g_peers[i].node_id;
    if (nid && nid < 8 && (now - g_peers[i].last_seen_ms) < PEER_TIMEOUT_MS)
      bits |= (1u << nid);
  }
  return bits;
}
void     bus_set_tx_mode(BusTxMode mode) { g_tx_mode = mode; }
BusTxMode bus_get_tx_mode()              { return g_tx_mode; }
void bus_set_observer(bus_observer_t cb) { g_observer = cb; }

// --------------------------------------------------------------
// LCD status widgets for CAN and WiFi health indicators
// --------------------------------------------------------------
#ifdef ENABLE_LCD

#ifndef BUS_CAN_WIDGET_ROW
#define BUS_CAN_WIDGET_ROW 0
#endif
#ifndef BUS_CAN_WIDGET_COL
#define BUS_CAN_WIDGET_COL 0
#endif
#ifndef BUS_ESPNOW_WIDGET_ROW
#define BUS_ESPNOW_WIDGET_ROW 0
#endif
#ifndef BUS_ESPNOW_WIDGET_COL
#define BUS_ESPNOW_WIDGET_COL 2
#endif
#ifndef BUS_AP_WIDGET_ROW
#define BUS_AP_WIDGET_ROW 0
#endif
#ifndef BUS_AP_WIDGET_COL
#define BUS_AP_WIDGET_COL 4
#endif

static void can_status_render(char* buf, uint8_t width) {
  buf[0] = 'C';
  if (width > 1) buf[1] = lcd_status_char(bus_can_healthy());
}

static void espnow_status_render(char* buf, uint8_t width) {
  buf[0] = 'N';
  if (width > 1) buf[1] = lcd_status_char(bus_wifi_seen_peer());
}

#if USE_WIFI
static void ap_status_render(char* buf, uint8_t width) {
  buf[0] = 'A';
  if (width > 1) buf[1] = lcd_status_char(WiFi.softAPIP() != IPAddress(0, 0, 0, 0));
}
#endif

void bus_register_lcd_widgets() {
  { LcdWidget w = { BUS_CAN_WIDGET_ROW, BUS_CAN_WIDGET_COL, 2, 1000, can_status_render }; lcd_register_widget(w); }
#if USE_WIFI
  { LcdWidget w = { BUS_ESPNOW_WIDGET_ROW, BUS_ESPNOW_WIDGET_COL, 2, 1000, espnow_status_render }; lcd_register_widget(w); }
  { LcdWidget w = { BUS_AP_WIDGET_ROW, BUS_AP_WIDGET_COL, 2, 1000, ap_status_render }; lcd_register_widget(w); }
#endif
}

#else
void bus_register_lcd_widgets() {}
#endif // ENABLE_LCD
