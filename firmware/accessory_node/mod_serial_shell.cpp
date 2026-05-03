// mod_serial_shell.cpp — interactive CAN shell over USB serial.
//
// Prints received frames and accepts hex CAN commands from the serial monitor.
// Uses Serial directly so output does NOT appear in the web UI Serial tab.
//
// Commands:
//   <id> [b0] [b1] ...         send a CAN frame (hex, same format as web UI)
//   :relay <n> on|off          relay 1–6 on or off
//   :alloff                    all relays off
//   :horn                      relay 5 on
//   :viper lock|unlock|start   send VIPER_CMD
//   mon                        toggle live frame monitor
//   help                       show command reference

#include <Arduino.h>
#include "mod_serial_shell.h"
#include "bus.h"
#include "can_protocol.h"

static bool    g_monitor = false;
static char    g_buf[64];
static uint8_t g_pos = 0;

// ---------------------------------------------------------------
// Prompt / line helpers
// ---------------------------------------------------------------
static void print_prompt() {
  Serial.print(F("> "));
}

// Called by serial_shell_print() when a frame arrives mid-input — keeps the
// prompt + in-progress line intact after the frame scrolls past.
static void reprint_input() {
  Serial.print(F("\r> "));
  Serial.write((const uint8_t*)g_buf, g_pos);
}

// ---------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------
static bool hex_byte(const char* s, uint8_t& out) {
  char* end;
  long v = strtol(s, &end, 16);
  if (end == s || v < 0 || v > 255) return false;
  out = (uint8_t)v;
  return true;
}

static void send_frame(uint32_t id, uint8_t* data, uint8_t dlc) {
  bus_tx(id, data, dlc);
  Serial.printf("TX %03X [%u]", id, dlc);
  for (uint8_t i = 0; i < dlc; i++) Serial.printf(" %02X", data[i]);
  Serial.println();
}

static void print_help() {
  Serial.println(F("--- CAN shell ---"));
  Serial.println(F("  <id> [b0 b1 ...]       send frame (hex)"));
  Serial.println(F("  :relay <n> on|off       relay 1–6"));
  Serial.println(F("  :alloff                 all relays off"));
  Serial.println(F("  :horn                   relay 5 on"));
  Serial.println(F("  :viper lock|unlock|start"));
  Serial.println(F("  mon                     toggle frame monitor"));
  Serial.println(F("  help                    this message"));
}

// ---------------------------------------------------------------
// Alias parser  (lines starting with ':')
// ---------------------------------------------------------------
static void parse_alias(char* line) {
  // skip leading ':'
  line++;
  while (*line == ' ') line++;

  // :relay <n> on|off
  if (strncmp(line, "relay", 5) == 0) {
    char* p = line + 5;
    while (*p == ' ') p++;
    long n = strtol(p, &p, 10);
    if (n < 1 || n > 6) { Serial.println(F("[shell] relay 1–6 only")); return; }
    while (*p == ' ') p++;
    uint8_t mask  = 1 << (n - 1);
    uint8_t state = (strncmp(p, "on", 2) == 0) ? mask : 0;
    uint8_t data[2] = { mask, state };
    send_frame(CAN_ID_RELAY_CMD, data, 2);
    return;
  }

  // :alloff
  if (strncmp(line, "alloff", 6) == 0) {
    uint8_t data[2] = { 0x3F, 0x00 };
    send_frame(CAN_ID_RELAY_CMD, data, 2);
    return;
  }

  // :horn  (relay 5 on — firmware watchdog cuts it at RELAY_MAX_ON_MS)
  if (strncmp(line, "horn", 4) == 0) {
    uint8_t data[2] = { 0x10, 0x10 };
    send_frame(CAN_ID_RELAY_CMD, data, 2);
    return;
  }

  // :viper lock|unlock|start
  if (strncmp(line, "viper", 5) == 0) {
    char* p = line + 5;
    while (*p == ' ') p++;
    uint8_t cmd = 0;
    if      (strncmp(p, "lock",   4) == 0) cmd = 0x01;
    else if (strncmp(p, "unlock", 6) == 0) cmd = 0x02;
    else if (strncmp(p, "start",  5) == 0) cmd = 0x03;
    else { Serial.println(F("[shell] viper: lock | unlock | start")); return; }
    send_frame(CAN_ID_VIPER_CMD, &cmd, 1);
    return;
  }

  Serial.printf("[shell] unknown alias ':%s'\n", line);
}

// ---------------------------------------------------------------
// Hex frame parser
// ---------------------------------------------------------------
static void parse_hex(char* line) {
  char* tok = strtok(line, " \t");
  if (!tok) return;
  char* end;
  long id = strtol(tok, &end, 16);
  if (end == tok || id < 0 || id > 0x7FF) {
    Serial.println(F("[shell] bad id (11-bit hex)"));
    return;
  }
  uint8_t data[8];
  uint8_t dlc = 0;
  while (dlc < 8 && (tok = strtok(nullptr, " \t")) != nullptr) {
    if (!hex_byte(tok, data[dlc])) {
      Serial.printf("[shell] bad byte '%s'\n", tok);
      return;
    }
    dlc++;
  }
  send_frame((uint32_t)id, data, dlc);
}

// ---------------------------------------------------------------
// Line dispatcher
// ---------------------------------------------------------------
static void dispatch(char* line) {
  while (*line == ' ') line++;
  if (*line == '\0') return;

  if (*line == ':')                        { parse_alias(line); return; }
  if (strncmp(line, "mon",  3) == 0)       { g_monitor = !g_monitor; Serial.printf("[shell] monitor %s\n", g_monitor ? "ON" : "OFF"); return; }
  if (strncmp(line, "help", 4) == 0)       { print_help(); return; }
  parse_hex(line);
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------
void serial_shell_setup() {
  Serial.println(F("[shell] CAN shell ready — 'help' for commands, 'mon' to watch frames"));
  print_prompt();
}

void serial_shell_tick() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\r') continue;

    if (c == '\n') {
      Serial.println();
      g_buf[g_pos] = '\0';
      g_pos = 0;
      dispatch(g_buf);
      print_prompt();
      return;
    }

    // Backspace (BS 0x08 or DEL 0x7F)
    if ((c == '\x08' || c == '\x7F') && g_pos > 0) {
      g_pos--;
      Serial.print(F("\x08 \x08"));
      return;
    }

    if (g_pos < sizeof(g_buf) - 1) {
      g_buf[g_pos++] = c;
      Serial.write(c);  // local echo
    }
  }
}

void serial_shell_print(const BusFrame& f) {
  if (!g_monitor) return;
  // Clear current input line, print the frame, restore prompt + in-progress input.
  Serial.print('\r');
  const char* dir = (strncmp(f.source, "self", 4) == 0) ? "TX" : "RX";
  Serial.printf("%s %03X [%u]", dir, f.id, f.dlc);
  for (uint8_t i = 0; i < f.dlc; i++) Serial.printf(" %02X", f.data[i]);
  Serial.println();
  reprint_input();
}
