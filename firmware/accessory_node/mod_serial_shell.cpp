// mod_serial_shell.cpp — interactive CAN shell over USB serial.
//
// Prints received frames and accepts hex CAN commands from the serial monitor.
// Uses Serial directly so output does NOT appear in the web UI Serial tab.
//
// Commands:
//   <id> [b0] [b1] ...   send a CAN frame (hex, same format as web UI)
//   mon                  toggle live frame monitor on/off (default: off)
//   help                 show command reference

#include <Arduino.h>
#include "mod_serial_shell.h"
#include "bus.h"
#include "can_protocol.h"

static bool   g_monitor = false;
static char   g_buf[40];
static uint8_t g_pos = 0;

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

static void print_help() {
  Serial.println(F("CAN shell commands:"));
  Serial.println(F("  <id> [b0] [b1]...  send frame (hex, up to 8 bytes)"));
  Serial.println(F("  mon                toggle frame monitor (currently shows all RX/TX)"));
  Serial.println(F("  help               this message"));
  Serial.println(F("Examples:"));
  Serial.println(F("  100 01 01          relay 1 on"));
  Serial.println(F("  100 3F 00          all relays off"));
  Serial.println(F("  510 01             viper arm"));
}

static void parse_and_send(char* line) {
  // Trim leading whitespace
  while (*line == ' ') line++;
  if (*line == '\0') return;

  // Named commands
  if (strncmp(line, "mon", 3) == 0) {
    g_monitor = !g_monitor;
    Serial.printf("[shell] monitor %s\n", g_monitor ? "ON" : "OFF");
    return;
  }
  if (strncmp(line, "help", 4) == 0) {
    print_help();
    return;
  }

  // Hex frame: first token is the CAN ID
  char* tok = strtok(line, " \t");
  if (!tok) return;
  char* end;
  long id = strtol(tok, &end, 16);
  if (end == tok || id < 0 || id > 0x7FF) {
    Serial.println(F("[shell] bad id (11-bit hex expected)"));
    return;
  }

  // Remaining tokens are data bytes
  uint8_t data[8];
  uint8_t dlc = 0;
  while (dlc < 8 && (tok = strtok(nullptr, " \t")) != nullptr) {
    if (!hex_byte(tok, data[dlc])) {
      Serial.printf("[shell] bad byte '%s'\n", tok);
      return;
    }
    dlc++;
  }

  bus_tx((uint32_t)id, data, dlc);
  Serial.printf("[shell] sent %03lX [%u]", id, dlc);
  for (uint8_t i = 0; i < dlc; i++) Serial.printf(" %02X", data[i]);
  Serial.println();
}

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------
void serial_shell_setup() {
  Serial.println(F("[shell] CAN shell ready — type 'help' for commands, 'mon' to watch frames"));
}

void serial_shell_tick() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      g_buf[g_pos] = '\0';
      parse_and_send(g_buf);
      g_pos = 0;
    } else if (g_pos < sizeof(g_buf) - 1) {
      g_buf[g_pos++] = c;
    } else {
      // Line too long — flush
      g_pos = 0;
    }
  }
}

void serial_shell_print(const BusFrame& f) {
  if (!g_monitor) return;
  const char* dir = (strncmp(f.source, "self", 4) == 0) ? "TX" : "RX";
  Serial.printf("%s %03X [%u]", dir, f.id, f.dlc);
  for (uint8_t i = 0; i < f.dlc; i++) Serial.printf(" %02X", f.data[i]);
  Serial.println();
}
