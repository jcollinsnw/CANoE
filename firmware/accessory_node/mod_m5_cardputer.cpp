// mod_m5_cardputer.cpp — M5Stack Cardputer TFT display + keyboard CLI module.
// Provides a portable CAN bus terminal with relay status display, frame log,
// and command injection via the built-in keyboard.

#include "node_config.h"

#ifdef ENABLE_M5_CARDPUTER

#include <M5Cardputer.h>
#include <Arduino.h>
#include "driver/twai.h"
#include "can_protocol.h"
#include "bus.h"
#include "node_state.h"

// --- Display layout ---
#define STATUS_BAR_H  25
#define SCREEN_W      240
#define SCREEN_H      135

// --- CLI state ---
static String g_cmd;
static bool   g_feed_active = true;
static uint8_t g_last_mirror = 0xFF;

// --- Macros ---
struct M5Macro { const char* shortcut; const char* expansion; };
static const M5Macro MACROS[] = {
  {"all_on",  "100 3F 3F"},
  {"all_off", "100 3F 00"},
};
static const int MACRO_COUNT = sizeof(MACROS) / sizeof(MACROS[0]);

static const char* CMDS[] = {"help", "cls", "status", "stopfeed", "startfeed"};
static const int CMD_COUNT = sizeof(CMDS) / sizeof(CMDS[0]);

// --- Forward declarations ---
static void draw_status_bar();
static void update_relay_display();
static void print_log(const String& text, uint16_t color = WHITE);
static void process_command(String cmd);
static void handle_tab();
static bool handle_hotkeys(const String& word);
static void inject_frame(const String& cmd);

// --- Public API ---

void m5_setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);

  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.fillScreen(BLACK);

  draw_status_bar();

  M5Cardputer.Display.setCursor(0, STATUS_BAR_H + 5);
  print_log("=== CANoE Cardputer [0x06] ===", GREEN);
  print_log("125kbps | WiFi Ch6 | ESP-NOW", DARKGREY);
  print_log("Ctrl+1-6: toggle relays", YELLOW);
  print_log(">", CYAN);
}

void m5_loop() {
  M5Cardputer.update();

  // Keyboard input
  if (M5Cardputer.Keyboard.isPressed()) {
    Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();

    if (ks.enter) {
      if (g_cmd.length() > 0) {
        print_log("\n> " + g_cmd, WHITE);
        process_command(g_cmd);
        g_cmd = "";
      }
      print_log("\n>", CYAN);
    } else if (ks.del && g_cmd.length() > 0) {
      g_cmd.remove(g_cmd.length() - 1);
      M5Cardputer.Display.print("\b \b");
    } else if (ks.tab || ks.word == "\t") {
      handle_tab();
    } else if (ks.word.length() > 0) {
      if (!handle_hotkeys(ks.word)) {
        g_cmd += ks.word;
        M5Cardputer.Display.print(ks.word);
      }
    }
  }

  // Redraw relay bar when mirror changes
  if (g_relay_mirror != g_last_mirror) {
    g_last_mirror = g_relay_mirror;
    update_relay_display();
  }
}

void m5_handle_frame(const BusFrame& f) {
  if (!g_feed_active) return;

  // Log frame to terminal
  char buf[64];
  int pos = snprintf(buf, sizeof(buf), "[%s] %03X", f.source, f.id);
  for (uint8_t i = 0; i < f.dlc && i < 8; i++)
    pos += snprintf(buf + pos, sizeof(buf) - pos, " %02X", f.data[i]);
  print_log(String(buf), LIGHTGREY);
}

// --- Status bar ---

static void draw_status_bar() {
  int cx = M5Cardputer.Display.getCursorX();
  int cy = M5Cardputer.Display.getCursorY();

  M5Cardputer.Display.fillRect(0, 0, SCREEN_W, STATUS_BAR_H, NAVY);
  M5Cardputer.Display.drawFastHLine(0, STATUS_BAR_H, SCREEN_W, DARKGREY);

  M5Cardputer.Display.setCursor(5, 6);
  M5Cardputer.Display.setTextColor(WHITE);
  M5Cardputer.Display.print("MESH:");
  M5Cardputer.Display.setTextColor(g_feed_active ? GREEN : RED);
  M5Cardputer.Display.print(g_feed_active ? "ON " : "OFF");

  // Relay indicators
  int startX = 75, boxW = 22, spacing = 26;
  for (int i = 0; i < 6; i++) {
    bool on = (g_relay_mirror >> i) & 1;
    uint16_t bg = on ? GREEN : DARKGREY;
    int x = startX + i * spacing;
    M5Cardputer.Display.fillRect(x, 4, boxW, 14, bg);
    M5Cardputer.Display.drawRect(x, 4, boxW, 14, WHITE);
    M5Cardputer.Display.setCursor(x + 7, 7);
    M5Cardputer.Display.setTextColor(on ? BLACK : WHITE);
    M5Cardputer.Display.print(String(i + 1));
  }

  M5Cardputer.Display.setCursor(cx, cy);
  M5Cardputer.Display.setTextColor(WHITE);
}

static void update_relay_display() {
  draw_status_bar();
}

// --- Terminal output ---

static void print_log(const String& text, uint16_t color) {
  M5Cardputer.Display.setTextColor(color);
  M5Cardputer.Display.println(text);

  if (M5Cardputer.Display.getCursorY() > (SCREEN_H - 10)) {
    M5Cardputer.Display.fillRect(0, STATUS_BAR_H + 1, SCREEN_W, SCREEN_H - STATUS_BAR_H - 1, BLACK);
    M5Cardputer.Display.setCursor(0, STATUS_BAR_H + 5);
  }
}

// --- Ctrl+1-6 relay hotkeys ---

static bool handle_hotkeys(const String& word) {
  if (word.length() != 1) return false;
  char c = word[0];
  if (c < 1 || c > 6) return false;

  uint8_t relay = c - 1;
  bool currently_on = (g_relay_mirror >> relay) & 1;
  uint8_t mask  = (1 << relay);
  uint8_t state = currently_on ? 0x00 : mask;

  uint8_t d[2] = { mask, state };
  bus_tx(CAN_ID_RELAY_CMD, d, 2);

  print_log("\n[hotkey] relay " + String(relay + 1) + (currently_on ? " OFF" : " ON"), ORANGE);
  print_log(">", CYAN);
  M5Cardputer.Display.print(g_cmd);
  return true;
}

// --- CLI ---

static void process_command(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  // Macro expansion
  for (int i = 0; i < MACRO_COUNT; i++) {
    if (cmd == MACROS[i].shortcut) {
      cmd = MACROS[i].expansion;
      print_log("[macro] -> " + cmd, DARKGREY);
      break;
    }
  }

  if (cmd == "help") {
    print_log("Commands: cls, status, stopfeed, startfeed", YELLOW);
    print_log("Frame:    <id> <b0> <b1> ...  (hex, spaces)", YELLOW);
    print_log("Macros:   all_on, all_off", YELLOW);
    return;
  }
  if (cmd == "cls") {
    M5Cardputer.Display.fillRect(0, STATUS_BAR_H + 1, SCREEN_W, SCREEN_H - STATUS_BAR_H - 1, BLACK);
    M5Cardputer.Display.setCursor(0, STATUS_BAR_H + 5);
    return;
  }
  if (cmd == "status") {
    twai_status_info_t info;
    twai_get_status_info(&info);
    print_log("CAN state=" + String(info.state) +
              " tx_err=" + String(info.tx_error_counter) +
              " rx_err=" + String(info.rx_error_counter), YELLOW);
    print_log("Peers: " + String(bus_peer_count()) +
              " CAN: " + String(bus_can_healthy() ? "OK" : "DOWN"), YELLOW);
    return;
  }
  if (cmd == "stopfeed") {
    g_feed_active = false;
    draw_status_bar();
    print_log("Frame feed muted.", ORANGE);
    return;
  }
  if (cmd == "startfeed") {
    g_feed_active = true;
    draw_status_bar();
    print_log("Frame feed active.", GREEN);
    return;
  }

  // Frame injection: "<id> <b0> <b1> ..." (space-separated hex)
  inject_frame(cmd);
}

static void inject_frame(const String& cmd) {
  // Parse space-separated hex: "100 3F 00"
  uint32_t can_id = 0;
  uint8_t data[8] = {};
  uint8_t dlc = 0;

  int start = 0;
  bool id_parsed = false;

  for (int i = 0; i <= (int)cmd.length(); i++) {
    if (i == (int)cmd.length() || cmd[i] == ' ') {
      if (i > start) {
        String token = cmd.substring(start, i);
        if (!id_parsed) {
          can_id = strtoul(token.c_str(), NULL, 16);
          id_parsed = true;
        } else if (dlc < 8) {
          data[dlc++] = strtoul(token.c_str(), NULL, 16);
        }
      }
      start = i + 1;
    }
  }

  if (!id_parsed) {
    print_log("Bad frame format. Use: <id> <b0> <b1> ...", RED);
    return;
  }

  bus_tx(can_id, data, dlc);
  print_log("[tx] OK", GREEN);
}

// --- Tab completion ---

static void handle_tab() {
  if (g_cmd.length() == 0) return;
  String match;
  int count = 0;

  for (int i = 0; i < CMD_COUNT; i++) {
    if (String(CMDS[i]).startsWith(g_cmd)) { match = CMDS[i]; count++; }
  }
  for (int i = 0; i < MACRO_COUNT; i++) {
    if (String(MACROS[i].shortcut).startsWith(g_cmd)) { match = MACROS[i].shortcut; count++; }
  }

  if (count == 1) {
    for (size_t i = 0; i < g_cmd.length(); i++) M5Cardputer.Display.print("\b \b");
    g_cmd = match;
    M5Cardputer.Display.print(g_cmd);
  } else if (count > 1) {
    print_log("", YELLOW);
    for (int i = 0; i < CMD_COUNT; i++)
      if (String(CMDS[i]).startsWith(g_cmd)) { M5Cardputer.Display.print(String(CMDS[i]) + " "); }
    for (int i = 0; i < MACRO_COUNT; i++)
      if (String(MACROS[i].shortcut).startsWith(g_cmd)) { M5Cardputer.Display.print(String(MACROS[i].shortcut) + " "); }
    print_log("\n>", CYAN);
    M5Cardputer.Display.print(g_cmd);
  }
}

#endif // ENABLE_M5_CARDPUTER
