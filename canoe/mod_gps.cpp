// mod_gps.cpp — GPS speed and heading via NMEA $GPRMC over hardware UART.
//
// Tested with u-blox Neo-6M / Neo-8M modules at 9600 baud.
// Only RX is needed — connect module TX → GPS_RX_PIN.
// GPS_TX_PIN can be set to -1 if you don't need to send commands to the module.
//
// Broadcasts CAN_ID_GPS_DATA every time a valid $GPRMC sentence arrives:
//   [speed_lo, speed_hi, heading_lo, heading_hi, flags]
//   speed   = 0.1 mph units (uint16 LE) — divide by 10 for mph
//   heading = 0.1 degree units (uint16 LE) — divide by 10 for degrees true
//   flags   = bit 0: fix valid (A = active), bit 1: speed valid, bit 2: heading valid

#include <Arduino.h>
#include "node_config.h"

#ifdef ENABLE_GPS

#include "can_protocol.h"
#include "bus.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

#ifndef GPS_BAUD
#define GPS_BAUD 9600
#endif
#ifndef GPS_TX_PIN
#define GPS_TX_PIN -1
#endif

static HardwareSerial g_gps(GPS_SERIAL_NUM);

// Line accumulation buffer — NMEA sentences are at most 82 chars
static char    g_buf[96];
static uint8_t g_buf_pos = 0;

// Extract the Nth comma-delimited field from a NMEA sentence into out[].
// Returns true if the field exists and fits in out_len.
static bool nmea_field(const char* sentence, uint8_t n, char* out, uint8_t out_len) {
  uint8_t field = 0;
  uint8_t i = 0, o = 0;
  while (sentence[i]) {
    if (sentence[i] == ',') {
      if (field == n) { out[o] = '\0'; return o > 0; }
      field++;
      i++;
      continue;
    }
    if (sentence[i] == '*') break;  // checksum delimiter
    if (field == n && o < out_len - 1) out[o++] = sentence[i];
    i++;
  }
  if (field == n) { out[o] = '\0'; return o > 0; }
  return false;
}

static void parse_gprmc(const char* sentence) {
  char field[16];

  // Field 1: status — 'A' = active fix, 'V' = void
  if (!nmea_field(sentence, 1, field, sizeof(field))) return;
  bool fix_valid = (field[0] == 'A');

  // Field 6: speed in knots → convert to 0.1 mph
  uint16_t speed_01mph = 0;
  bool speed_valid = false;
  if (nmea_field(sentence, 6, field, sizeof(field)) && field[0]) {
    float knots = atof(field);
    speed_01mph = (uint16_t)(knots * 11.5078f);  // knots × 1.15078 × 10
    speed_valid = true;
  }

  // Field 7: true heading in degrees → convert to 0.1 degree units
  uint16_t heading_01deg = 0;
  bool heading_valid = false;
  if (nmea_field(sentence, 7, field, sizeof(field)) && field[0]) {
    float hdg = atof(field);
    heading_01deg = (uint16_t)(hdg * 10.0f);
    heading_valid = true;
  }

  uint8_t flags = (fix_valid ? 0x01 : 0) |
                  (speed_valid   ? 0x02 : 0) |
                  (heading_valid ? 0x04 : 0);

  uint8_t d[5];
  pack_u16(&d[0], speed_01mph);
  pack_u16(&d[2], heading_01deg);
  d[4] = flags;
  bus_tx(CAN_ID_GPS_DATA, d, 5);

  if (fix_valid)
    wlog("[gps] %.1f mph  hdg %.1f deg\n",
         speed_01mph / 10.0f, heading_01deg / 10.0f);
  else
    wlogln("[gps] no fix");
}

static void process_line() {
  // Must start with $GPRMC (or $GNRMC on multi-constellation modules)
  if (strncmp(g_buf, "$GPRMC", 6) != 0 &&
      strncmp(g_buf, "$GNRMC", 6) != 0) return;
  parse_gprmc(g_buf);
}

void gps_setup() {
  g_gps.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  wlogln("[gps] setup OK");
}

void gps_loop() {
  while (g_gps.available()) {
    char c = (char)g_gps.read();
    if (c == '\n' || c == '\r') {
      if (g_buf_pos > 0) {
        g_buf[g_buf_pos] = '\0';
        process_line();
        g_buf_pos = 0;
      }
    } else if (g_buf_pos < sizeof(g_buf) - 1) {
      g_buf[g_buf_pos++] = c;
    } else {
      g_buf_pos = 0;  // overflow — discard and resync
    }
  }
}

#endif // ENABLE_GPS
