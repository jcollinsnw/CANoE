// mod_mqtt.cpp — MQTT bridge for CAN frames.
// Requires PubSubClient library: arduino-cli lib install "PubSubClient"
// Only compiled when MQTT_BROKER is defined in node_config.h.

#ifdef MQTT_BROKER

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "node_config.h"
#include "bus.h"

#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_TOPIC_PREFIX
#define MQTT_TOPIC_PREFIX "canbus"
#endif
#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID "bridge"
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASS
#define MQTT_PASS ""
#endif

static WiFiClient   g_wc;
static PubSubClient g_mqtt(g_wc);

static char g_topic_frames[64];
static char g_topic_send[64];

// Parse {"id":N,"data":[b0,...]} and inject onto bus.
static void on_message(char* /*topic*/, byte* payload, unsigned int len) {
  if (len == 0 || len > 200) return;
  char buf[201];
  memcpy(buf, payload, len);
  buf[len] = 0;
  String body(buf);

  uint32_t id = 0;
  uint8_t data[8]; uint8_t dlc = 0;

  int pos = body.indexOf("\"id\"");
  if (pos < 0) return;
  int colon = body.indexOf(':', pos);
  if (colon < 0) return;
  id = (uint32_t)strtoul(body.c_str() + colon + 1, nullptr, 10);
  if (!id) return;

  int lb = body.indexOf('['), rb = body.indexOf(']');
  if (lb < 0 || rb <= lb) return;
  const char* p = body.c_str() + lb + 1;
  while (p < body.c_str() + rb && dlc < 8) {
    while (*p == ' ' || *p == ',') p++;
    if (p >= body.c_str() + rb) break;
    char* end = nullptr;
    long v = strtol(p, &end, 10);
    if (end == p) break;
    data[dlc++] = (uint8_t)(v & 0xFF);
    p = end;
  }
  if (dlc > 0) bus_tx(id, data, dlc);
}

static bool do_connect() {
  const char* user = strlen(MQTT_USER) ? MQTT_USER : nullptr;
  const char* pass = strlen(MQTT_PASS) ? MQTT_PASS : nullptr;
  if (!g_mqtt.connect(MQTT_CLIENT_ID, user, pass)) return false;
  g_mqtt.subscribe(g_topic_send);
  Serial.printf("[mqtt] connected broker=%s:%d prefix=%s\n",
                MQTT_BROKER, MQTT_PORT, MQTT_TOPIC_PREFIX);
  return true;
}

void mqtt_setup() {
  snprintf(g_topic_frames, sizeof(g_topic_frames), "%s/frames", MQTT_TOPIC_PREFIX);
  snprintf(g_topic_send,   sizeof(g_topic_send),   "%s/send",   MQTT_TOPIC_PREFIX);
  g_mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  g_mqtt.setCallback(on_message);
  g_mqtt.setBufferSize(256);
}

static uint32_t g_last_reconnect_ms = 0;

void mqtt_tick() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!g_mqtt.connected()) {
    uint32_t now = millis();
    if (now - g_last_reconnect_ms < 5000) return;
    g_last_reconnect_ms = now;
    if (!do_connect())
      Serial.printf("[mqtt] connect failed, retry in 5 s (state=%d)\n", g_mqtt.state());
    return;
  }
  g_mqtt.loop();
}

void mqtt_handle_frame(const BusFrame& f) {
  if (!g_mqtt.connected()) return;
  // Skip self-echoes — publish each unique frame exactly once.
  if (strncmp(f.source, "self", 4) == 0) return;

  char buf[160];
  int pos = snprintf(buf, sizeof(buf),
    "{\"id\":%lu,\"dlc\":%u,\"src\":\"%s\",\"data\":[",
    (unsigned long)f.id, (unsigned)f.dlc, f.source);
  for (uint8_t i = 0; i < f.dlc && pos < (int)sizeof(buf) - 4; i++) {
    if (i) buf[pos++] = ',';
    pos += snprintf(buf + pos, sizeof(buf) - pos, "%u", f.data[i]);
  }
  if (pos < (int)sizeof(buf) - 2) { buf[pos++] = ']'; buf[pos++] = '}'; buf[pos] = 0; }
  g_mqtt.publish(g_topic_frames, buf);
}

#endif // MQTT_BROKER
