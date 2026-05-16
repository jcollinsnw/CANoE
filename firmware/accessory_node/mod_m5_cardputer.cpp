// mod_m5_cardputer.cpp — M5Stack Cardputer TFT display + keyboard module.
// Feed screen: scrolling CAN frame log; 1-6 toggle relays (when CLI closed);
//              fn=menu, ctrl=toggle CLI bar.
// Menu screen: action macros; up=';' down='.' navigate, enter selects, del/fn=back.
// CLI bar: fixed bottom strip; ctrl opens/closes it; while open 1-6 type normally.

#include "node_config.h"

#ifdef ENABLE_M5_CARDPUTER

#include <M5Cardputer.h>
#include <Arduino.h>
#ifdef ENABLE_SD_LOG
#include <SD.h>
#include <FS.h>
#endif
#include "driver/twai.h"
#include "can_protocol.h"
#include "bus.h"
#include "node_state.h"

// --- Display layout ---
#define STATUS_BAR_H       22  // reduced from 25; relay boxes (h=14) are now vertically centred
#define SCREEN_W           240
#define SCREEN_H           135
#define CLI_BAR_H          14
#define CLI_BAR_Y          (SCREEN_H - CLI_BAR_H)                      // 121
#define MENU_ITEM_H        10
#define MENU_START_Y       (STATUS_BAR_H + 2)                          // 24
#define VISIBLE_MENU_ITEMS ((CLI_BAR_Y - MENU_START_Y) / MENU_ITEM_H) // 9
#define LINE_H             10
#define FEED_Y             (STATUS_BAR_H + 1)                          // 23
#define FEED_H             (CLI_BAR_Y - FEED_Y)                        // 98
#define MAX_LINES          (FEED_H / LINE_H)                           // 9
#define LINE_BUF_SZ        40

// Brand blue (#006DAC) in RGB565: ((0&0xF8)<<8)|((0x6D&0xFC)<<3)|(0xAC>>3) = 0x0375
#define ACAPULCO_BLUE      0x0375

// --- Scroll buffer ---
struct LogLine { char text[LINE_BUF_SZ]; uint16_t color; };
static LogLine  g_lines[MAX_LINES];
static uint8_t  g_line_head  = 0;
static uint8_t  g_line_count = 0;

// --- Screen state ---
enum class M5Screen { FEED, MENU };
static M5Screen g_screen      = M5Screen::FEED;
static uint8_t  g_last_mirror = 0xFF;
static int32_t  g_batt_pct   = -1;
static uint32_t g_batt_last  = 0;

// --- SD logging state ---
static bool g_logging = false;
#ifdef ENABLE_SD_LOG
static File g_log_file;
#endif

// --- CLI state ---
static bool   g_cli_active = false;
static String g_cli_input;
static char   g_event_msg[32] = {};

// --- CLI history ---
#define CLI_HIST_SZ  8
static String  g_history[CLI_HIST_SZ];
static uint8_t g_hist_head  = 0;   // next-write slot (ring)
static uint8_t g_hist_count = 0;   // number of valid entries
static int8_t  g_hist_pos   = -1;  // -1 = not browsing; 0 = newest, 1 = older…
static String  g_cli_draft;        // saved input before history browse began

// --- Menu state ---
enum ArgType : uint8_t { ARG_NONE, ARG_RELAY_ON, ARG_RELAY_OFF, ARG_HEX };

struct MenuItem {
    const char* label;
    ArgType      arg;
};

static const MenuItem MENU_ITEMS[] = {
    {"All Relays OFF",   ARG_NONE},
    {"All Relays ON",    ARG_NONE},
    {"Horn (relay 5)",   ARG_NONE},
    {"Relay N ON",       ARG_RELAY_ON},
    {"Relay N OFF",      ARG_RELAY_OFF},
    {"Viper Lock",       ARG_NONE},
    {"Viper Unlock",     ARG_NONE},
    {"Viper Start",      ARG_NONE},
    {"Log START",        ARG_NONE},
    {"Reboot All",       ARG_NONE},
    {"Reboot Node",      ARG_HEX},
};
static const int MENU_COUNT = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);

static int    g_menu_sel    = 0;
static int    g_menu_scroll = 0;
static bool   g_menu_in_arg = false;
static String g_menu_arg;

// --- Forward declarations ---
static void draw_status_bar();
static void draw_cli_bar();
static void redraw_feed();
static void enter_feed();
static void enter_menu();
static void draw_menu();
static void print_log(const String& text, uint16_t color = WHITE);
static void execute_menu_item(int sel, const String& arg);
static void handle_feed_keys(const Keyboard_Class::KeysState& ks);
static void handle_menu_keys(const Keyboard_Class::KeysState& ks);
static void inject_frame(const String& cmd);
static void log_start();
static void log_stop();
static void history_push(const String& cmd);
static void history_up();
static void history_down();

// --- Public event API ---

void m5_set_event(const char* msg) {
    strncpy(g_event_msg, msg, sizeof(g_event_msg) - 1);
    g_event_msg[sizeof(g_event_msg) - 1] = '\0';
    if (!g_cli_active) draw_cli_bar();
}

// --- Public API ---

void m5_setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.fillScreen(BLACK);

    draw_status_bar();
    draw_cli_bar();

    M5Cardputer.Display.setCursor(0, STATUS_BAR_H + 5);
    print_log("=== CANoE Cardputer [0x06] ===",   GREEN);
    print_log("125kbps | WiFi Ch6 | ESP-NOW",       DARKGREY);
    print_log("1-6: relay | fn: menu | ctrl: cli",  YELLOW);
}

void m5_loop() {
    M5Cardputer.update();

    // Poll battery level every 5 s
    uint32_t now = millis();
    if (now - g_batt_last >= 5000) {
        g_batt_last = now;
        int32_t pct = M5Cardputer.Power.getBatteryLevel();
        if (pct != g_batt_pct) {
            g_batt_pct = pct;
            draw_status_bar();
        }
    }

    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
        Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();

        // fn always toggles feed/menu (closes CLI if open)
        if (ks.fn) {
            if (g_cli_active) { g_cli_active = false; g_cli_input = ""; }
            if (g_screen == M5Screen::FEED) enter_menu();
            else                            enter_feed();
            return;
        }

        // ctrl alone toggles CLI bar (feed mode only)
        if (ks.ctrl && ks.word.empty() && g_screen == M5Screen::FEED) {
            g_cli_active = !g_cli_active;
            g_cli_input  = "";
            draw_cli_bar();
            return;
        }

        if (g_screen == M5Screen::FEED) handle_feed_keys(ks);
        else                            handle_menu_keys(ks);
    }

    if (g_relay_mirror != g_last_mirror) {
        g_last_mirror = g_relay_mirror;
        draw_status_bar();
        if (g_screen == M5Screen::MENU) draw_menu();
    }
}

void m5_handle_frame(const BusFrame& f) {
    // Status events shown in the CLI bar
    if (f.id == CAN_ID_RELAY_CMD && f.dlc >= 2) {
        uint8_t mask = f.data[0], state = f.data[1];
        char msg[24];
        if (mask == 0x3F && state == 0x00) {
            m5_set_event("All relays OFF");
        } else {
            for (uint8_t i = 0; i < 6; i++) {
                if (mask & (1u << i)) {
                    snprintf(msg, sizeof(msg), "Relay %d %s", i + 1,
                             (state & (1u << i)) ? "ON" : "OFF");
                    m5_set_event(msg);
                    break;
                }
            }
        }
    }

    // SD log regardless of active screen
#ifdef ENABLE_SD_LOG
    if (g_logging && g_log_file) {
        char line[80];
        int pos = snprintf(line, sizeof(line), "%lu,%s,%03X,%u",
                           millis(), f.source, (unsigned)f.id, f.dlc);
        for (uint8_t i = 0; i < f.dlc && i < 8; i++)
            pos += snprintf(line + pos, sizeof(line) - pos, ",%02X", f.data[i]);
        g_log_file.println(line);
    }
#endif

    if (g_screen != M5Screen::FEED) return;

    uint16_t color = LIGHTGREY;
    if      (strcmp(f.source, "wifi") == 0) color = GREEN;
    else if (strcmp(f.source, "self") == 0) color = CYAN;

    char buf[64];
    int pos = snprintf(buf, sizeof(buf), "[%s] %03X", f.source, f.id);
    for (uint8_t i = 0; i < f.dlc && i < 8; i++)
        pos += snprintf(buf + pos, sizeof(buf) - pos, " %02X", f.data[i]);
    print_log(String(buf), color);
}

// --- Screen transitions ---

static void enter_feed() {
    g_screen = M5Screen::FEED;
    draw_status_bar();
    redraw_feed();
    draw_cli_bar();
}

static void enter_menu() {
    g_screen      = M5Screen::MENU;
    g_menu_in_arg = false;
    g_menu_arg    = "";
    draw_status_bar();
    draw_menu();
    draw_cli_bar();
}

// --- Status bar ---

static void draw_status_bar() {
    int cx = M5Cardputer.Display.getCursorX();
    int cy = M5Cardputer.Display.getCursorY();

    M5Cardputer.Display.fillRect(0, 0, SCREEN_W, STATUS_BAR_H, ACAPULCO_BLUE);
    M5Cardputer.Display.drawFastHLine(0, STATUS_BAR_H, SCREEN_W, DARKGREY);

    M5Cardputer.Display.setCursor(5, (STATUS_BAR_H - 8) / 2);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.print(g_screen == M5Screen::MENU ? "MENU" : "FEED");

    int startX = 55, boxW = 18, spacing = 20;
    for (int i = 0; i < 6; i++) {
        bool on = (g_relay_mirror >> i) & 1;
        int  x  = startX + i * spacing;
        M5Cardputer.Display.fillRect(x, 4, boxW, 14, on ? GREEN : DARKGREY);
        M5Cardputer.Display.drawRect(x, 4, boxW, 14, WHITE);
        M5Cardputer.Display.setCursor(x + 6, 7);
        M5Cardputer.Display.setTextColor(on ? BLACK : WHITE);
        M5Cardputer.Display.print(String(i + 1));
    }

    // Battery
    if (g_batt_pct >= 0) {
        int bx = 182, by = 4, bw = 30, bh = 14;
        uint16_t col = g_batt_pct > 20 ? GREEN : (g_batt_pct > 10 ? YELLOW : RED);
        M5Cardputer.Display.drawRect(bx, by, bw, bh, WHITE);
        M5Cardputer.Display.fillRect(bx + bw, by + 4, 2, bh - 8, WHITE);
        int fill = (bw - 2) * g_batt_pct / 100;
        if (fill > 0)
            M5Cardputer.Display.fillRect(bx + 1, by + 1, fill, bh - 2, col);
        char pbuf[5];
        snprintf(pbuf, sizeof(pbuf), "%d%%", (int)g_batt_pct);
        int tw = strlen(pbuf) * 6;
        M5Cardputer.Display.setCursor(bx + (bw - tw) / 2, by + 3);
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.print(pbuf);
    }

    // REC indicator
    if (g_logging) {
        M5Cardputer.Display.setCursor(218, 0);
        M5Cardputer.Display.setTextColor(RED);
        M5Cardputer.Display.print("REC");
    }

    // Peer count
    uint8_t peers = bus_peer_count();
    M5Cardputer.Display.setCursor(218, 7);
    M5Cardputer.Display.setTextColor(peers > 0 ? GREEN : RED);
    char nbuf[4];
    snprintf(nbuf, sizeof(nbuf), "P%d", peers);
    M5Cardputer.Display.print(nbuf);

    M5Cardputer.Display.setCursor(cx, cy);
    M5Cardputer.Display.setTextColor(WHITE);
}

// --- CLI bar ---

static void draw_cli_bar() {
    uint16_t bg = g_cli_active ? (uint16_t)0x1082 : (uint16_t)0x0841;
    M5Cardputer.Display.fillRect(0, CLI_BAR_Y, SCREEN_W, CLI_BAR_H, bg);
    M5Cardputer.Display.drawFastHLine(0, CLI_BAR_Y, SCREEN_W, DARKGREY);

    M5Cardputer.Display.setCursor(2, CLI_BAR_Y + 3);
    if (g_cli_active) {
        M5Cardputer.Display.setTextColor(GREEN);
        M5Cardputer.Display.print("> ");
        M5Cardputer.Display.setTextColor(WHITE);
        M5Cardputer.Display.print(g_cli_input);
        M5Cardputer.Display.print("_");
    } else if (g_event_msg[0]) {
        M5Cardputer.Display.setTextColor(YELLOW);
        M5Cardputer.Display.print(g_event_msg);
    } else {
        M5Cardputer.Display.setTextColor(DARKGREY);
        M5Cardputer.Display.print("[ctrl] cli");
    }
}

// --- Menu rendering ---

static void draw_menu() {
    // Clear only the area between status bar and CLI bar
    M5Cardputer.Display.fillRect(0, STATUS_BAR_H + 1, SCREEN_W,
                                 CLI_BAR_Y - STATUS_BAR_H - 1, BLACK);

    for (int i = 0; i < VISIBLE_MENU_ITEMS; i++) {
        int item = g_menu_scroll + i;
        if (item >= MENU_COUNT) break;

        int  y   = MENU_START_Y + i * MENU_ITEM_H;
        bool sel = (item == g_menu_sel) && !g_menu_in_arg;

        if (sel)
            M5Cardputer.Display.fillRect(0, y, SCREEN_W, MENU_ITEM_H, DARKGREY);

        M5Cardputer.Display.setCursor(2, y + 1);
        M5Cardputer.Display.setTextColor(sel ? YELLOW : WHITE);

        if (item == 8)
            M5Cardputer.Display.print(g_logging ? "Log STOP [REC]" : "Log START");
        else
            M5Cardputer.Display.print(MENU_ITEMS[item].label);
    }

    if (g_menu_in_arg) {
        int row = g_menu_sel - g_menu_scroll;
        if (row >= 0 && row < VISIBLE_MENU_ITEMS) {
            int y = MENU_START_Y + row * MENU_ITEM_H;
            M5Cardputer.Display.fillRect(0, y, SCREEN_W, MENU_ITEM_H, NAVY);
            M5Cardputer.Display.setCursor(2, y + 1);
            M5Cardputer.Display.setTextColor(CYAN);

            ArgType at = MENU_ITEMS[g_menu_sel].arg;
            if (at == ARG_RELAY_ON || at == ARG_RELAY_OFF)
                M5Cardputer.Display.print("Relay (1-6): _");
            else {
                M5Cardputer.Display.print("Node ID (hex): ");
                M5Cardputer.Display.print(g_menu_arg);
                M5Cardputer.Display.print("_");
            }
        }
    }

    // Scrollbar: 3-px strip on right edge
    if (MENU_COUNT > VISIBLE_MENU_ITEMS) {
        int track_h = VISIBLE_MENU_ITEMS * MENU_ITEM_H;
        int thumb_h = max(2, track_h * VISIBLE_MENU_ITEMS / MENU_COUNT);
        int thumb_y = MENU_START_Y + track_h * g_menu_scroll / MENU_COUNT;
        M5Cardputer.Display.fillRect(SCREEN_W - 3, MENU_START_Y,    3, track_h,  DARKGREY);
        M5Cardputer.Display.fillRect(SCREEN_W - 3, thumb_y,         3, thumb_h,  WHITE);
    }
}

// --- Feed screen ---

static void redraw_feed() {
    M5Cardputer.Display.fillRect(0, FEED_Y, SCREEN_W, FEED_H, BLACK);
    uint8_t start = (g_line_count < MAX_LINES) ? 0 : g_line_head;
    uint8_t n     = (g_line_count < MAX_LINES) ? g_line_count : MAX_LINES;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (start + i) % MAX_LINES;
        M5Cardputer.Display.setCursor(1, FEED_Y + i * LINE_H + 1);
        M5Cardputer.Display.setTextColor(g_lines[idx].color);
        M5Cardputer.Display.print(g_lines[idx].text);
    }
}

static void print_log(const String& text, uint16_t color) {
    LogLine& line = g_lines[g_line_head];
    strncpy(line.text, text.c_str(), LINE_BUF_SZ - 1);
    line.text[LINE_BUF_SZ - 1] = '\0';
    line.color     = color;
    g_line_head    = (g_line_head + 1) % MAX_LINES;
    if (g_line_count < MAX_LINES) g_line_count++;
    if (g_screen == M5Screen::FEED) redraw_feed();
}

// --- CLI history helpers ---

static void history_push(const String& cmd) {
    if (cmd.length() == 0) return;
    // Skip duplicate of most-recent entry.
    if (g_hist_count > 0 &&
        g_history[(g_hist_head - 1 + CLI_HIST_SZ) % CLI_HIST_SZ] == cmd) return;
    g_history[g_hist_head] = cmd;
    g_hist_head = (g_hist_head + 1) % CLI_HIST_SZ;
    if (g_hist_count < CLI_HIST_SZ) g_hist_count++;
    g_hist_pos = -1;
}

static void history_up() {
    if (g_hist_count == 0) return;
    if (g_hist_pos == -1) g_cli_draft = g_cli_input;  // save draft on first up
    if (g_hist_pos < (int8_t)g_hist_count - 1) {
        g_hist_pos++;
        g_cli_input = g_history[(g_hist_head - 1 - g_hist_pos + CLI_HIST_SZ * 2) % CLI_HIST_SZ];
        draw_cli_bar();
    }
}

static void history_down() {
    if (g_hist_pos < 0) return;
    if (g_hist_pos == 0) {
        g_hist_pos  = -1;
        g_cli_input = g_cli_draft;
    } else {
        g_hist_pos--;
        g_cli_input = g_history[(g_hist_head - 1 - g_hist_pos + CLI_HIST_SZ * 2) % CLI_HIST_SZ];
    }
    draw_cli_bar();
}

// --- Feed key handling ---

static void handle_feed_keys(const Keyboard_Class::KeysState& ks) {
    if (g_cli_active) {
        // CLI open: 1-6 type normally, no relay hotkeys
        if (ks.enter) {
            String cmd = g_cli_input;
            cmd.trim();
            g_cli_input = "";
            g_hist_pos  = -1;
            if (cmd == "cls") {
                g_line_head  = 0;
                g_line_count = 0;
                redraw_feed();
            } else if (cmd.length() > 0) {
                history_push(cmd);
                inject_frame(cmd);
            }
            draw_cli_bar();
        } else if (ks.del) {
            g_hist_pos = -1;
            if (g_cli_input.length() > 0) {
                g_cli_input.remove(g_cli_input.length() - 1);
                draw_cli_bar();
            }
        } else if (!ks.word.empty()) {
            if (ks.word.size() == 1 && ks.word[0] == ';') {
                history_up();
            } else if (ks.word.size() == 1 && ks.word[0] == '.') {
                history_down();
            } else {
                g_hist_pos = -1;  // typing breaks out of history browse
                g_cli_input += String(ks.word.data(), ks.word.size());
                draw_cli_bar();
            }
        }
        return;
    }

    // CLI closed: 1-6 toggle relays
    if (ks.word.empty()) return;
    char c = ks.word[0];
    if (c < '1' || c > '6') return;

    uint8_t relay = c - '1';
    bool    on    = (g_relay_mirror >> relay) & 1;
    uint8_t mask  = 1 << relay;
    uint8_t d[2]  = {mask, on ? (uint8_t)0x00 : mask};
    bus_tx(CAN_ID_RELAY_CMD, d, 2);
    print_log("[relay " + String(relay + 1) + (on ? " OFF]" : " ON]"), ORANGE);
}

// --- CLI frame injection ---

static void inject_frame(const String& cmd) {
    uint32_t can_id   = 0;
    uint8_t  data[8]  = {};
    uint8_t  dlc      = 0;
    int      start    = 0;
    bool     id_parsed = false;

    for (int i = 0; i <= (int)cmd.length(); i++) {
        if (i == (int)cmd.length() || cmd[i] == ' ') {
            if (i > start) {
                String tok = cmd.substring(start, i);
                if (!id_parsed) { can_id = strtoul(tok.c_str(), NULL, 16); id_parsed = true; }
                else if (dlc < 8) data[dlc++] = strtoul(tok.c_str(), NULL, 16);
            }
            start = i + 1;
        }
    }

    if (!id_parsed) { print_log("[cli] bad frame", RED); return; }
    bus_tx(can_id, data, dlc);
}

// --- Menu execution ---

static void execute_menu_item(int sel, const String& arg) {
    uint8_t d[2];
    switch (sel) {
        case 0:  d[0] = 0x3F; d[1] = 0x00; bus_tx(CAN_ID_RELAY_CMD,  d, 2); break; // All OFF
        case 1:  d[0] = 0x3F; d[1] = 0x3F; bus_tx(CAN_ID_RELAY_CMD,  d, 2); break; // All ON
        case 2:  d[0] = 0x10; d[1] = 0x10; bus_tx(CAN_ID_RELAY_CMD,  d, 2); break; // Horn
        case 3:  {                                                                     // Relay N ON
            uint8_t mask = 1 << (arg[0] - '1');
            d[0] = mask; d[1] = mask;
            bus_tx(CAN_ID_RELAY_CMD, d, 2);
            break;
        }
        case 4:  {                                                                     // Relay N OFF
            uint8_t mask = 1 << (arg[0] - '1');
            d[0] = mask; d[1] = 0x00;
            bus_tx(CAN_ID_RELAY_CMD, d, 2);
            break;
        }
        case 5:  { uint8_t v = 0x01; bus_tx(CAN_ID_VIPER_CMD,  &v, 1); break; }      // Viper Lock
        case 6:  { uint8_t v = 0x02; bus_tx(CAN_ID_VIPER_CMD,  &v, 1); break; }      // Viper Unlock
        case 7:  { uint8_t v = 0x03; bus_tx(CAN_ID_VIPER_CMD,  &v, 1); break; }      // Viper Start
        case 8:  if (g_logging) log_stop(); else log_start(); break;                  // Log toggle
        case 9:  { uint8_t v = 0xFF; bus_tx(CAN_ID_REBOOT_CMD, &v, 1); break; }      // Reboot All
        case 10: {                                                                     // Reboot Node
            uint8_t v = (uint8_t)strtoul(arg.c_str(), NULL, 16);
            bus_tx(CAN_ID_REBOOT_CMD, &v, 1);
            break;
        }
    }
}

// --- Menu key handling ---

static void handle_menu_keys(const Keyboard_Class::KeysState& ks) {
    if (g_menu_in_arg) {
        ArgType at = MENU_ITEMS[g_menu_sel].arg;

        if (at == ARG_RELAY_ON || at == ARG_RELAY_OFF) {
            if (!ks.word.empty()) {
                char c = ks.word[0];
                if (c >= '1' && c <= '6') {
                    execute_menu_item(g_menu_sel, String(c));
                    g_menu_in_arg = false;
                    g_menu_arg    = "";
                    draw_menu();
                }
            }
            if (ks.del) { g_menu_in_arg = false; draw_menu(); }
            return;
        }

        // ARG_HEX: typed input + enter
        if (ks.enter) {
            if (g_menu_arg.length() > 0)
                execute_menu_item(g_menu_sel, g_menu_arg);
            g_menu_in_arg = false;
            g_menu_arg    = "";
            draw_menu();
        } else if (ks.del) {
            if (g_menu_arg.length() > 0) {
                g_menu_arg.remove(g_menu_arg.length() - 1);
                draw_menu();
            } else {
                g_menu_in_arg = false;
                draw_menu();
            }
        } else if (!ks.word.empty() && g_menu_arg.length() < 4) {
            g_menu_arg += String(ks.word.data(), ks.word.size());
            draw_menu();
        }
        return;
    }

    // Browsing
    if (ks.del) { enter_feed(); return; }

    if (!ks.word.empty()) {
        char c = ks.word[0];

        if (c == ';') {  // up arrow
            g_menu_sel = (g_menu_sel - 1 + MENU_COUNT) % MENU_COUNT;
            if (g_menu_sel < g_menu_scroll)
                g_menu_scroll = g_menu_sel;
            else if (g_menu_sel == MENU_COUNT - 1)
                g_menu_scroll = max(0, MENU_COUNT - VISIBLE_MENU_ITEMS);
            draw_menu();
            return;
        }
        if (c == '.') {  // down arrow
            g_menu_sel = (g_menu_sel + 1) % MENU_COUNT;
            if (g_menu_sel >= g_menu_scroll + VISIBLE_MENU_ITEMS)
                g_menu_scroll = g_menu_sel - VISIBLE_MENU_ITEMS + 1;
            else if (g_menu_sel == 0)
                g_menu_scroll = 0;
            draw_menu();
            return;
        }
        // Relay hotkeys work from menu too (CLI is closed while in menu)
        if (c >= '1' && c <= '6') {
            uint8_t relay = c - '1';
            bool    on    = (g_relay_mirror >> relay) & 1;
            uint8_t mask  = 1 << relay;
            uint8_t d[2]  = {mask, on ? (uint8_t)0x00 : mask};
            bus_tx(CAN_ID_RELAY_CMD, d, 2);
            return;
        }
    }

    if (ks.enter) {
        if (MENU_ITEMS[g_menu_sel].arg == ARG_NONE) {
            execute_menu_item(g_menu_sel, "");
        } else {
            g_menu_in_arg = true;
            g_menu_arg    = "";
            draw_menu();
        }
    }
}

// --- SD logging ---

#ifdef ENABLE_SD_LOG
static void log_start() {
    if (g_logging) return;
    if (!SD.begin(SS, SPI, 25000000)) { print_log("[SD] mount failed", RED); return; }

    char path[24];
    for (int i = 1; i <= 999; i++) {
        snprintf(path, sizeof(path), "/canlog_%03d.csv", i);
        if (!SD.exists(path)) break;
    }

    g_log_file = SD.open(path, FILE_WRITE);
    if (!g_log_file) { print_log("[SD] open failed", RED); return; }

    g_log_file.println("ms,source,id,dlc,d0,d1,d2,d3,d4,d5,d6,d7");
    g_logging = true;
    print_log(String("[SD] ") + path, GREEN);
    draw_status_bar();
}

static void log_stop() {
    if (!g_logging) return;
    g_log_file.flush();
    g_log_file.close();
    g_logging = false;
    print_log("[SD] log stopped", YELLOW);
    draw_status_bar();
}
#else
static void log_start() { print_log("[SD] add ENABLE_SD_LOG to enable", RED); }
static void log_stop()  {}
#endif

#endif // ENABLE_M5_CARDPUTER
