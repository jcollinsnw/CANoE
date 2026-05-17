// webui.cpp
// SoftAP + captive portal DNS redirect + HTTP server + JSON API.
// Ties into the bus via webui_observe() so the web UI sees every frame.
//
// The AP SSID is the same on every node ("AccessoryBus") so your phone
// picks whichever node is closest. Both nodes default to channel 6 so
// ESP-NOW can bridge them regardless of which one you're connected to.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <stdarg.h>
#include "node_config.h"
#include "can_protocol.h"
#include "bus.h"
#include "webui.h"
#include "index_html.h"
#include "mod_wifi_creds.h"
#include "mod_blob.h"
#include <Update.h>
#ifdef ENABLE_RULES
#include "mod_rules.h"
#endif

// --------------------------------------------------------------
// Config — AP settings can be overridden in the node's config header:
//   #define AP_SSID     "MyNetwork"   // default: "AccessoryBus"
//   #define AP_PASSWORD "mypassword"  // default: open (no password)
//   #define AP_HIDDEN   1             // default: 0 (visible SSID)
// --------------------------------------------------------------
#ifndef AP_SSID
#define AP_SSID     "AccessoryBus"
#endif
#ifndef AP_PASSWORD
#define AP_PASSWORD ""
#endif
#ifndef AP_HIDDEN
#define AP_HIDDEN   0
#endif
static const int    AP_CHANNEL  = 6;
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_NETMASK(255, 255, 255, 0);

// --------------------------------------------------------------
// Frame ring buffer for the web UI
// --------------------------------------------------------------
struct LogFrame {
  uint32_t seq;       // monotonic cursor the browser pages through
  uint32_t t_ms;      // millis() at capture
  uint32_t can_id;
  uint8_t  dlc;
  uint8_t  data[8];
  bool     outbound;
  char     source[8];
};

static const int UI_LOG_SIZE = 128;
static LogFrame  g_log[UI_LOG_SIZE];
static volatile uint32_t g_log_next_seq = 1;
static volatile int      g_log_head = 0;

// --------------------------------------------------------------
// Serial tee — wlog() / wlogln() write to both Serial and a
// ring buffer that the web UI reads via /api/serial.
// --------------------------------------------------------------
static const int SLOG_SIZE = 4096;          // character ring
static char      g_slog[SLOG_SIZE];
static volatile uint32_t g_slog_write = 0;  // total chars written (cursor)

static void slog_append(const char* buf, int len) {
  for (int i = 0; i < len; i++) {
    g_slog[g_slog_write % SLOG_SIZE] = buf[i];
    g_slog_write++;
  }
}

// --------------------------------------------------------------
// State
// --------------------------------------------------------------
static WebServer g_http(80);
static DNSServer g_dns;
static const char* g_node_name = "unknown";
static uint8_t     g_node_id = 0;
static uint32_t    g_boot_ms = 0;
static uint8_t     g_ap_clients = 0;
static void (*g_ap_client_cb)(uint8_t, uint8_t) = nullptr;
static bool        g_ota_error = false;

// --------------------------------------------------------------
// Node capability cache — populated from CAN_ID_NODE_CAP (0x0F2) frames.
// --------------------------------------------------------------
struct NodeCapEntry {
  uint8_t  node_id;
  uint8_t  caps;
  uint8_t  switch_count;
  uint8_t  button_count;
  uint8_t  led_count;
  uint8_t  relay_count;
  uint32_t last_ms;
  bool     valid;
};
static NodeCapEntry g_ncaps[8];

// --------------------------------------------------------------
// JSON helpers (tiny — we emit by hand to avoid pulling in a lib)
// --------------------------------------------------------------
static void append_hex_byte(String& s, uint8_t v) {
  char b[3]; sprintf(b, "%02x", v); s += b;
}

// --------------------------------------------------------------
// HTTP handlers
// --------------------------------------------------------------
static void handle_root() {
  g_http.sendHeader("Cache-Control", "no-store");
  g_http.send_P(200, "text/html", INDEX_HTML);
}

static void handle_status() {
  uint32_t uptime_s = (millis() - g_boot_ms) / 1000;
  String s = "{";
  s += "\"name\":\""; s += g_node_name; s += "\",";
  s += "\"id\":";     s += g_node_id;           s += ",";
  s += "\"can_ok\":"; s += (bus_can_healthy() ? "true" : "false"); s += ",";
  s += "\"wifi_peer\":"; s += (bus_wifi_seen_peer() ? "true" : "false"); s += ",";
  s += "\"tx_mode\":";   s += (uint8_t)bus_get_tx_mode(); s += ",";
  s += "\"peer_nodes\":"; s += bus_peer_node_bitmap(); s += ",";
  {
    uint8_t ids[8]; uint8_t cnt = bus_get_peer_ids(ids, 8);
    s += "\"peer_ids\":[";
    for (uint8_t i = 0; i < cnt; i++) { if (i) s += ","; s += ids[i]; }
    s += "],";
  }
  s += "\"uptime_s\":"; s += uptime_s;
  s += "}";
  g_http.send(200, "application/json", s);
}

static void handle_frames() {
  uint32_t since = 0;
  if (g_http.hasArg("since")) since = (uint32_t)g_http.arg("since").toInt();

  String s;
  s.reserve(4096);
  s += "{\"cursor\":"; s += g_log_next_seq - 1; s += ",\"frames\":[";

  // Walk buffer from oldest to newest; include only frames with seq > since
  bool first = true;
  for (int i = 0; i < UI_LOG_SIZE; i++) {
    const LogFrame& f = g_log[i];
    if (f.seq == 0 || f.seq <= since) continue;
    if (!first) s += ',';
    first = false;
    s += "{\"seq\":"; s += f.seq; s += ",";
    s += "\"t\":";    s += f.t_ms; s += ",";
    s += "\"id\":";   s += f.can_id; s += ",";
    s += "\"dlc\":";  s += f.dlc;    s += ",";
    s += "\"out\":";  s += (f.outbound ? "true":"false"); s += ",";
    s += "\"src\":\""; s += f.source; s += "\",";
    s += "\"data\":[";
    for (uint8_t b = 0; b < f.dlc; b++) {
      if (b) s += ',';
      s += f.data[b];
    }
    s += "]}";
  }
  s += "]}";

  g_http.sendHeader("Cache-Control", "no-store");
  g_http.send(200, "application/json", s);
}

// Very small JSON parser for {"id":N,"data":[..]}
// Accepts integers only (not 0x-hex) because JS side converts for us.
static bool parse_send_body(const String& body, uint32_t& id, uint8_t* out, uint8_t& dlc) {
  int pos_id = body.indexOf("\"id\"");
  int pos_dt = body.indexOf("\"data\"");
  if (pos_id < 0 || pos_dt < 0) return false;
  int colon = body.indexOf(':', pos_id);
  if (colon < 0) return false;
  id = (uint32_t)strtoul(body.c_str() + colon + 1, nullptr, 10);

  int lb = body.indexOf('[', pos_dt);
  int rb = body.indexOf(']', pos_dt);
  if (lb < 0 || rb < 0 || rb <= lb) return false;

  dlc = 0;
  const char* p = body.c_str() + lb + 1;
  while (p < body.c_str() + rb && dlc < 8) {
    while (*p == ' ' || *p == ',' || *p == '\t') p++;
    if (p >= body.c_str() + rb) break;
    char* end = nullptr;
    long v = strtol(p, &end, 10);
    if (end == p) break;
    out[dlc++] = (uint8_t)(v & 0xFF);
    p = end;
  }
  return true;
}

static void handle_send() {
  if (!g_http.hasArg("plain")) { g_http.send(400, "text/plain", "no body"); return; }
  const String& body = g_http.arg("plain");
  uint32_t id = 0;
  uint8_t  data[8]; uint8_t dlc = 0;
  if (!parse_send_body(body, id, data, dlc)) {
    g_http.send(400, "text/plain", "bad json");
    return;
  }
  bus_tx(id, data, dlc);
  g_http.send(200, "application/json", "{\"ok\":true}");
}

static void handle_tx_mode() {
  if (!g_http.hasArg("plain")) { g_http.send(400, "text/plain", "no body"); return; }
  const String& body = g_http.arg("plain");
  long v = -1;
  // parse {"mode":N}
  int pos = body.indexOf("\"mode\"");
  if (pos >= 0) {
    pos = body.indexOf(':', pos);
    if (pos >= 0) v = body.substring(pos + 1).toInt();
  }
  if (v < 0 || v > 2) { g_http.send(400, "text/plain", "mode must be 0,1,2"); return; }
  bus_set_tx_mode((BusTxMode)v);
  g_http.send(200, "application/json", "{\"ok\":true}");
}

static void handle_serial() {
  // Client sends ?since=<cursor>.  We return new bytes since that cursor.
  uint32_t since = 0;
  if (g_http.hasArg("since")) since = (uint32_t)strtoul(g_http.arg("since").c_str(), nullptr, 10);
  uint32_t head = g_slog_write;
  // Clamp: if client is too far behind, start from oldest available
  if (head - since > SLOG_SIZE) since = head - SLOG_SIZE;
  uint32_t avail = head - since;

  String s;
  s.reserve(avail + 64);
  s += "{\"cursor\":"; s += String((unsigned long)head); s += ",\"text\":\"";
  for (uint32_t i = since; i < head; i++) {
    char c = g_slog[i % SLOG_SIZE];
    if (c == '"')       s += "\\\"";
    else if (c == '\\') s += "\\\\";
    else if (c == '\n') s += "\\n";
    else if (c == '\r') ; // skip CR
    else if (c >= 0x20 && c < 0x7F) s += c;
    else s += '.';
  }
  s += "\"}";
  g_http.sendHeader("Cache-Control", "no-store");
  g_http.send(200, "application/json", s);
}

static void handle_config() {
  static const char* const LABELS[] = {
#ifdef RELAY_1_LABEL
    RELAY_1_LABEL,
#else
    nullptr,
#endif
#ifdef RELAY_2_LABEL
    RELAY_2_LABEL,
#else
    nullptr,
#endif
#ifdef RELAY_3_LABEL
    RELAY_3_LABEL,
#else
    nullptr,
#endif
#ifdef RELAY_4_LABEL
    RELAY_4_LABEL,
#else
    nullptr,
#endif
#ifdef RELAY_5_LABEL
    RELAY_5_LABEL,
#else
    nullptr,
#endif
#ifdef RELAY_6_LABEL
    RELAY_6_LABEL,
#else
    nullptr,
#endif
  };

  String s = "{";
#ifdef BRIDGE_MODE
  s += "\"is_bridge\":true,";
#else
  s += "\"is_bridge\":false,";
#endif
#ifdef ENABLE_RELAY
  s += "\"has_relay\":true,";
#else
  s += "\"has_relay\":false,";
#endif
#ifdef ENABLE_SWITCHES
  s += "\"has_switches\":true,";
  s += "\"switch_count\":" + String(NUM_SWITCHES) + ",";
  s += "\"button_count\":" + String(NUM_BUTTONS) + ",";
#else
  s += "\"has_switches\":false,\"switch_count\":0,\"button_count\":0,";
#endif
#ifdef ENABLE_VIPER
  s += "\"has_viper\":true,";
#else
  s += "\"has_viper\":false,";
#endif
#ifdef ENABLE_LEDS
  s += "\"has_leds\":true,";
  s += "\"led_count\":" + String(NUM_LEDS) + ",";
#else
  s += "\"has_leds\":false,\"led_count\":0,";
#endif
#ifdef ENABLE_RULES
  s += "\"has_rules\":true,";
#else
  s += "\"has_rules\":false,";
#endif
  s += "\"relay_labels\":[";
  for (int i = 0; i < 6; i++) {
    if (i) s += ',';
    s += '"';
    if (LABELS[i]) {
      for (const char* p = LABELS[i]; *p; p++) {
        if (*p == '"' || *p == '\\') s += '\\';
        s += *p;
      }
    } else {
      s += "Relay "; s += (char)('1' + i);
    }
    s += '"';
  }
  s += "]}";
  g_http.sendHeader("Cache-Control", "no-store");
  g_http.send(200, "application/json", s);
}

#ifdef ENABLE_RULES
// GET /api/rules — return all rule slots as a JSON array.
static void handle_rules_get() {
  String s;
  s.reserve(512);
  s += "[";
  uint8_t n = rules_max();
  for (uint8_t i = 0; i < n; i++) {
    CanRule r = rules_get(i);
    if (i) s += ",";
    s += "{\"i\":"; s += i;
    s += ",\"trig_id\":"; s += r.trig_id;
    s += ",\"c0_byte\":"; s += r.c0_byte;
    s += ",\"c0_val\":";  s += r.c0_val;
    s += ",\"c0_mask\":"; s += r.c0_mask;
    s += ",\"c1_byte\":"; s += r.c1_byte;
    s += ",\"c1_val\":";  s += r.c1_val;
    s += ",\"c1_mask\":"; s += r.c1_mask;
    s += ",\"action\":";  s += r.action;
    s += ",\"arg0\":";    s += r.arg0;
    s += ",\"arg1\":";    s += r.arg1;
    s += ",\"arg2\":";    s += r.arg2;
    s += "}";
  }
  s += "]";
  g_http.sendHeader("Cache-Control", "no-store");
  g_http.send(200, "application/json", s);
}

// Tiny integer field extractor: finds "key":N in a JSON string.
static bool json_int(const String& body, const char* key, long& out) {
  String needle = String("\"") + key + "\":";
  int pos = body.indexOf(needle);
  if (pos < 0) return false;
  pos += needle.length();
  while (pos < (int)body.length() && body[pos] == ' ') pos++;
  char* end = nullptr;
  out = strtol(body.c_str() + pos, &end, 10);
  return end != body.c_str() + pos;
}

// POST /api/rules — upsert a rule slot. Body is JSON with all CanRule fields + "i" (index).
static void handle_rules_post() {
  if (!g_http.hasArg("plain")) { g_http.send(400, "text/plain", "no body"); return; }
  const String& body = g_http.arg("plain");

  long idx = -1;
  if (!json_int(body, "i", idx) || idx < 0 || idx >= rules_max()) {
    g_http.send(400, "text/plain", "bad index"); return;
  }

  CanRule r = rules_get((uint8_t)idx);  // start from current so unset fields are preserved
  long v;
  if (json_int(body, "trig_id", v)) r.trig_id  = (uint16_t)v;
  if (json_int(body, "c0_byte", v)) r.c0_byte  = (uint8_t)v;
  if (json_int(body, "c0_val",  v)) r.c0_val   = (uint8_t)v;
  if (json_int(body, "c0_mask", v)) r.c0_mask  = (uint8_t)v;
  if (json_int(body, "c1_byte", v)) r.c1_byte  = (uint8_t)v;
  if (json_int(body, "c1_val",  v)) r.c1_val   = (uint8_t)v;
  if (json_int(body, "c1_mask", v)) r.c1_mask  = (uint8_t)v;
  if (json_int(body, "action",  v)) r.action   = (uint8_t)v;
  if (json_int(body, "arg0",    v)) r.arg0     = (uint8_t)v;
  if (json_int(body, "arg1",    v)) r.arg1     = (uint8_t)v;
  if (json_int(body, "arg2",    v)) r.arg2     = (uint8_t)v;

  // Emit as blob so the change appears in the frame log and cross-node tools can observe it.
  // Self-echo is ignored by blob_handle_frame(), so we also apply locally below.
  {
    uint8_t buf[sizeof(CanRule)];
    memcpy(buf, &r, sizeof(CanRule));
    blob_send(g_node_id, BLOB_NS_RULES, (uint8_t)idx, buf, sizeof(CanRule), BLOB_FLAG_PERSIST);
  }
  rules_set((uint8_t)idx, r);
  g_http.send(200, "application/json", "{\"ok\":true}");
}

// DELETE /api/rules?i=N — clear a rule slot.
static void handle_rules_delete() {
  if (!g_http.hasArg("i")) { g_http.send(400, "text/plain", "missing i"); return; }
  long idx = g_http.arg("i").toInt();
  if (idx < 0 || idx >= rules_max()) { g_http.send(400, "text/plain", "bad index"); return; }
  // Zeroed CanRule signals "clear this slot" to the blob callback on remote nodes.
  {
    static const uint8_t empty[sizeof(CanRule)] = {};
    blob_send(g_node_id, BLOB_NS_RULES, (uint8_t)idx, empty, sizeof(CanRule), BLOB_FLAG_PERSIST);
  }
  rules_clear((uint8_t)idx);
  g_http.send(200, "application/json", "{\"ok\":true}");
}

// POST /api/rules/reset — restore compiled defaults.
static void handle_rules_reset() {
  // key 0xFE is the factory-reset sentinel for BLOB_NS_RULES.
  static const uint8_t sentinel[1] = { 0xFF };
  blob_send(g_node_id, BLOB_NS_RULES, 0xFE, sentinel, 1, BLOB_FLAG_PERSIST);
  rules_reset_factory();
  g_http.send(200, "application/json", "{\"ok\":true}");
}
#endif // ENABLE_RULES

static void handle_nodecaps() {
  String s = "[";
  bool first = true;
  for (int i = 0; i < 8; i++) {
    if (!g_ncaps[i].valid) continue;
    if (!first) s += ",";
    first = false;
    s += "{\"id\":";           s += g_ncaps[i].node_id;
    s += ",\"caps\":";         s += g_ncaps[i].caps;
    s += ",\"switch_count\":"; s += g_ncaps[i].switch_count;
    s += ",\"button_count\":"; s += g_ncaps[i].button_count;
    s += ",\"led_count\":";    s += g_ncaps[i].led_count;
    s += ",\"relay_count\":";  s += g_ncaps[i].relay_count;
    s += ",\"age_ms\":";       s += (millis() - g_ncaps[i].last_ms);
    s += "}";
  }
  s += "]";
  g_http.sendHeader("Cache-Control", "no-store");
  g_http.send(200, "application/json", s);
}

// ---------- wifi creds helpers ----------
static bool hex_to_bytes(const String& hex, uint8_t* out, size_t len) {
  if ((size_t)hex.length() != len * 2) return false;
  for (size_t i = 0; i < len; i++) {
    auto hval = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    int h = hval(hex[i*2]), l = hval(hex[i*2+1]);
    if (h < 0 || l < 0) return false;
    out[i] = (uint8_t)((h << 4) | l);
  }
  return true;
}

static bool json_str_val(const String& body, const char* key, String& out) {
  String needle = String("\"") + key + "\":\"";
  int pos = body.indexOf(needle);
  if (pos < 0) return false;
  pos += needle.length();
  // Handle escaped quotes minimally
  int end = pos;
  while (end < (int)body.length() && !(body[end] == '"' && (end == 0 || body[end-1] != '\\'))) end++;
  out = body.substring(pos, end);
  return true;
}

static bool json_bool_val(const String& body, const char* key, bool& out) {
  String needle = String("\"") + key + "\":";
  int pos = body.indexOf(needle);
  if (pos < 0) return false;
  pos += needle.length();
  while (pos < (int)body.length() && body[pos] == ' ') pos++;
  out = (body.substring(pos, pos + 4) == "true");
  return true;
}

static void handle_wifi_creds_get() {
  char ssid[33], pass[65];
  uint8_t pmk[16], lmk[16];
  wifi_creds_get_ssid(ssid, sizeof(ssid));
  wifi_creds_get_pass(pass, sizeof(pass));
  wifi_creds_get_pmk(pmk);
  wifi_creds_get_lmk(lmk);
  String s = "{\"ssid\":\""; s += ssid; s += "\",\"pass\":\""; s += pass; s += "\",\"pmk\":\"";
  for (int i = 0; i < 16; i++) { char b[3]; sprintf(b, "%02x", pmk[i]); s += b; }
  s += "\",\"lmk\":\"";
  for (int i = 0; i < 16; i++) { char b[3]; sprintf(b, "%02x", lmk[i]); s += b; }
  s += "\"}";
  g_http.sendHeader("Cache-Control", "no-store");
  g_http.send(200, "application/json", s);
}

static void handle_wifi_creds_post() {
  if (!g_http.hasArg("plain")) { g_http.send(400, "text/plain", "no body"); return; }
  const String& body = g_http.arg("plain");

  String ssid_s, pass_s, pmk_s, lmk_s;
  bool has_ssid = json_str_val(body, "ssid", ssid_s);
  bool has_pass = json_str_val(body, "pass", pass_s);
  bool has_pmk  = json_str_val(body, "pmk",  pmk_s);
  bool has_lmk  = json_str_val(body, "lmk",  lmk_s);
  bool broadcast = false;
  json_bool_val(body, "broadcast", broadcast);

  if (has_ssid && (ssid_s.length() == 0 || ssid_s.length() > 32)) {
    g_http.send(400, "text/plain", "ssid: 1-32 chars"); return;
  }
  if (has_pass && pass_s.length() > 0 && pass_s.length() < 8) {
    g_http.send(400, "text/plain", "pass: empty or >=8 chars"); return;
  }
  uint8_t pmk[16] = {}, lmk[16] = {};
  if (has_pmk && !hex_to_bytes(pmk_s, pmk, 16)) {
    g_http.send(400, "text/plain", "pmk: need 32 hex chars"); return;
  }
  if (has_lmk && !hex_to_bytes(lmk_s, lmk, 16)) {
    g_http.send(400, "text/plain", "lmk: need 32 hex chars"); return;
  }

  if (has_ssid) wifi_creds_set_ssid(ssid_s.c_str());
  if (has_pass) wifi_creds_set_pass(pass_s.c_str());
  if (has_pmk)  wifi_creds_set_pmk(pmk);
  if (has_lmk)  wifi_creds_set_lmk(lmk);
  wifi_creds_save();

  if (broadcast) wifi_creds_broadcast(0xFF, BLOB_FLAG_PERSIST);

  g_http.send(200, "application/json", "{\"ok\":true}");
}

static void handle_wifi_creds_reset() {
  wifi_creds_reset();
  g_http.send(200, "application/json", "{\"ok\":true}");
}

static void handle_ota_upload() {
  HTTPUpload& upload = g_http.upload();
  if (upload.status == UPLOAD_FILE_START) {
    g_ota_error = false;
    wlog("[ota] start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      g_ota_error = true;
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!g_ota_error) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        g_ota_error = true;
        Update.printError(Serial);
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!g_ota_error) {
      if (Update.end(true)) {
        wlog("[ota] success: %u bytes\n", upload.totalSize);
      } else {
        g_ota_error = true;
        Update.printError(Serial);
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    g_ota_error = true;
    Update.abort();
    wlog("[ota] aborted\n");
  }
}

static void handle_ota() {
  g_http.sendHeader("Connection", "close");
  if (g_ota_error) {
    g_http.send(500, "text/plain", "FAIL");
  } else {
    g_http.send(200, "text/plain", "OK");
    delay(100);
    ESP.restart();
  }
}

static void handle_not_found() {
  // Captive portal: any unknown host → redirect to our root
  g_http.sendHeader("Location", String("http://") + AP_IP.toString() + "/", true);
  g_http.send(302, "text/plain", "");
}

// --------------------------------------------------------------
// Public
// --------------------------------------------------------------
void webui_init(const char* node_name, uint8_t node_id) {
  g_node_name = node_name;
  g_node_id   = node_id;
  g_boot_ms   = millis();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK);
  // Same SSID on every node so the phone roams; channel is fixed so
  // ESP-NOW across nodes just works.
  char ap_ssid[33], ap_pass[65];
  wifi_creds_get_ssid(ap_ssid, sizeof(ap_ssid));
  wifi_creds_get_pass(ap_pass, sizeof(ap_pass));
  WiFi.softAP(ap_ssid, strlen(ap_pass) ? ap_pass : nullptr, AP_CHANNEL, AP_HIDDEN);
  delay(100);
  Serial.printf("[wifi] AP up  SSID=\"%s\"%s  IP=%s  ch=%d\n",
                AP_HIDDEN ? "<hidden>" : ap_ssid,
                strlen(ap_pass) ? " (WPA2)" : " (open)",
                WiFi.softAPIP().toString().c_str(), AP_CHANNEL);
#ifdef BRIDGE_MODE
  // Connect to existing router so bridge is reachable from home network.
  WiFi.begin(STA_SSID, STA_PASSWORD);
  Serial.printf("[wifi] STA connecting to \"%s\"...\n", STA_SSID);
#endif

  // Captive DNS: wildcard resolve → our AP IP
  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  g_dns.start(53, "*", AP_IP);

  // Routes
  g_http.on("/",            HTTP_GET,  handle_root);
  g_http.on("/api/status",  HTTP_GET,  handle_status);
  g_http.on("/api/frames",  HTTP_GET,  handle_frames);
  g_http.on("/api/send",    HTTP_POST, handle_send);
  g_http.on("/api/tx_mode",  HTTP_POST, handle_tx_mode);
  g_http.on("/api/serial",  HTTP_GET,  handle_serial);
  g_http.on("/api/config",   HTTP_GET,  handle_config);
  g_http.on("/api/nodecaps", HTTP_GET,  handle_nodecaps);
#ifdef ENABLE_RULES
  g_http.on("/api/rules",       HTTP_GET,    handle_rules_get);
  g_http.on("/api/rules",       HTTP_POST,   handle_rules_post);
  g_http.on("/api/rules",       HTTP_DELETE, handle_rules_delete);
  g_http.on("/api/rules/reset", HTTP_POST,   handle_rules_reset);
#else
  g_http.on("/api/rules", HTTP_GET, [](){
    g_http.sendHeader("Cache-Control", "no-store");
    g_http.send(200, "application/json", "[]");
  });
#endif

  g_http.on("/api/wifi_creds",       HTTP_GET,  handle_wifi_creds_get);
  g_http.on("/api/wifi_creds",       HTTP_POST, handle_wifi_creds_post);
  g_http.on("/api/wifi_creds/reset", HTTP_POST, handle_wifi_creds_reset);

  g_http.on("/api/ota", HTTP_POST, handle_ota, handle_ota_upload);

  // Common captive-portal probe paths — just bounce them to our root
  g_http.on("/generate_204",         HTTP_GET, handle_not_found);
  g_http.on("/gen_204",              HTTP_GET, handle_not_found);
  g_http.on("/hotspot-detect.html",  HTTP_GET, handle_not_found);
  g_http.on("/connecttest.txt",      HTTP_GET, handle_not_found);
  g_http.on("/ncsi.txt",             HTTP_GET, handle_not_found);
  g_http.onNotFound(handle_not_found);

  g_http.begin();
  Serial.println("[http] server up on :80");

  bus_set_observer(webui_observe);
}

void webui_set_ap_client_cb(void (*cb)(uint8_t new_count, uint8_t old_count)) {
  g_ap_client_cb = cb;
}

void webui_tick() {
  g_dns.processNextRequest();
  g_http.handleClient();
  {
    uint8_t n = (uint8_t)WiFi.softAPgetStationNum();
    if (n != g_ap_clients) {
      uint8_t old = g_ap_clients;
      g_ap_clients = n;
      wlog("[wifi] AP clients: %u -> %u\n", old, n);
      if (g_ap_client_cb) g_ap_client_cb(n, old);
    }
  }
#ifdef BRIDGE_MODE
  static uint8_t sta_state = 0;
  if (sta_state == 0) {
    wl_status_t ws = WiFi.status();
    if (ws == WL_CONNECTED) {
      sta_state = 1;
      Serial.printf("[wifi] STA connected, IP=%s\n", WiFi.localIP().toString().c_str());
    } else if (ws == WL_CONNECT_FAILED || ws == WL_NO_SSID_AVAIL) {
      sta_state = 2;
      Serial.printf("[wifi] STA connect failed (status=%d)\n", (int)ws);
    }
  }
#endif
}

void webui_observe(const BusFrame& f, bool outbound) {
  LogFrame& slot = g_log[g_log_head];
  slot.seq = g_log_next_seq++;
  slot.t_ms = millis();
  slot.can_id = f.id;
  slot.dlc = f.dlc;
  memcpy(slot.data, f.data, 8);
  slot.outbound = outbound;
  strncpy(slot.source, f.source, sizeof(slot.source));
  slot.source[sizeof(slot.source) - 1] = 0;
  g_log_head = (g_log_head + 1) % UI_LOG_SIZE;
}

void webui_serial_tee_install() {
  // No-op — serial tee is now handled by wlog()/wlogln().
  // Kept for backward compat; safe to call but does nothing.
}

int wlog(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
  slog_append(buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
  return n;
}

void wlogln(const char* msg) {
  Serial.println(msg);
  slog_append(msg, strlen(msg));
  slog_append("\n", 1);
}

void webui_handle_node_cap(const BusFrame& f) {
  if (f.dlc < 6) return;
  uint8_t nid = f.data[0];
  for (int i = 0; i < 8; i++) {
    if (g_ncaps[i].valid && g_ncaps[i].node_id != nid) continue;
    g_ncaps[i] = { nid, f.data[1], f.data[2], f.data[3], f.data[4], f.data[5], millis(), true };
    return;
  }
}
