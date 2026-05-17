// mod_m5_cardputer.cpp — M5Stack Cardputer TFT display + keyboard module.
// Feed screen: scrolling CAN frame log; 1-6 toggle relays (when CLI closed);
//              fn=menu, ctrl=toggle CLI bar.
// Menu screen: action macros; up=';' down='.' navigate, enter selects, del/fn=back.
// CLI bar: fixed bottom strip; ctrl opens/closes it; while open 1-6 type normally.

#include "node_config.h"

#ifdef ENABLE_M5_CARDPUTER

#include <M5Cardputer.h>
#include <Arduino.h>
#include <Preferences.h>
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

// --- Scroll buffers ---
struct LogLine { char text[LINE_BUF_SZ]; uint16_t color; };
static LogLine  g_can_lines[MAX_LINES];
static uint8_t  g_can_head  = 0;
static uint8_t  g_can_count = 0;
static LogLine  g_feed_lines[MAX_LINES];
static uint8_t  g_feed_head  = 0;
static uint8_t  g_feed_count = 0;

// --- Screen state ---
enum class M5Screen { CAN, FEED, MENU, SETTINGS };
static M5Screen g_screen      = M5Screen::CAN;
static uint8_t  g_last_mirror = 0xFF;
static int32_t  g_batt_pct   = -1;
static uint32_t g_batt_last  = 0;

// --- Settings state ---
// Speaker volume is persisted locally; BT/WiFi toggles broadcast and track last-sent state.
static const uint8_t SPK_VOL_TABLE[]  = {0, 40, 80, 160, 240};
static const char*   SPK_VOL_LABELS[] = {"MUTE", "LOW", "MED", "HIGH", "MAX"};
static const int     SPK_VOL_COUNT    = sizeof(SPK_VOL_TABLE) / sizeof(SPK_VOL_TABLE[0]);
static uint8_t g_spk_vol_idx   = 2;     // MED
static bool    g_bt_enabled    = true;  // assume relay_controller boots with BT on
static bool    g_wifi_enabled  = true;
static Preferences g_prefs;

#define SET_ITEM_BT    0
#define SET_ITEM_WIFI  1
#define SET_ITEM_SPK   2
#define SET_COUNT      3
static int g_set_sel = 0;

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

// --- Status bar selection state ---
#define SB_ITEMS         7   // 6 relays (0-5) + FEED/MENU label (6)
#define SB_ITEM_FEEDMENU 6
static bool   g_sb_sel_active = false;
static int8_t g_sb_sel        = 0;   // 0-5 = relay index, 6 = FEED/MENU
static bool   g_sb_dropdown   = false;
static int8_t g_sb_dd_sel     = 0;

// --- Menu state ---
enum ArgType : uint8_t { ARG_NONE, ARG_RELAY_ON, ARG_RELAY_OFF, ARG_HEX };

struct MenuItem {
    const char* label;
    ArgType      arg;
};

static const char* SB_DROPDOWN_ITEMS[] = {"CAN", "FEED", "COMMANDS", "SETTINGS"};
static const int   SB_DD_COUNT = 4;

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
static void draw_sb_dropdown();
static void draw_cli_bar();
static void redraw_can();
static void redraw_feed();
static void enter_can();
static void enter_feed();
static void enter_menu();
static void enter_settings();
static void draw_menu();
static void draw_settings();
static void handle_settings_keys(const Keyboard_Class::KeysState& ks);
static void apply_setting(int item);
static void print_can(const String& text, uint16_t color = WHITE);
static void print_feed(const String& text, uint16_t color = WHITE);
static bool decode_frame(const BusFrame& f, char* buf, size_t sz);
static void execute_menu_item(int sel, const String& arg);
static void handle_sb_sel_keys(const Keyboard_Class::KeysState& ks);
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

    g_prefs.begin("m5card", true);
    g_spk_vol_idx = g_prefs.getUChar("spk_vol", 2);
    g_prefs.end();
    if (g_spk_vol_idx >= SPK_VOL_COUNT) g_spk_vol_idx = 2;

    draw_status_bar();
    draw_cli_bar();

    M5Cardputer.Display.setCursor(0, STATUS_BAR_H + 5);
    print_can("=== CANoE Cardputer [0x06] ===",    GREEN);
    print_can("125kbps | WiFi Ch6 | ESP-NOW",        DARKGREY);
    print_can("1-6: relay | fn: cmds | opt: statusbar", YELLOW);
}

void m5_beep_startup() {
    M5Cardputer.Speaker.setVolume(80);
    M5Cardputer.Speaker.tone(440, 80);  delay(90);
    M5Cardputer.Speaker.tone(660, 80);  delay(90);
    M5Cardputer.Speaker.tone(880, 180); delay(190);
}

void m5_beep_alert() {
    M5Cardputer.Speaker.setVolume(200);
    for (int i = 0; i < 3; i++) {
        M5Cardputer.Speaker.tone(1047, 100); delay(200);
    }
}

void m5_beep_can_up() {
    M5Cardputer.Speaker.setVolume(80);
    M5Cardputer.Speaker.tone(660, 80);  delay(90);
    M5Cardputer.Speaker.tone(880, 120); delay(130);
}

void m5_beep_can_down() {
    M5Cardputer.Speaker.setVolume(80);
    M5Cardputer.Speaker.tone(880, 80);  delay(90);
    M5Cardputer.Speaker.tone(440, 120); delay(130);
}

void m5_beep_peer(uint8_t count) {
    M5Cardputer.Speaker.setVolume(60);
    M5Cardputer.Speaker.tone(count > 0 ? 880 : 440, 80); delay(90);
}

static void m5_ui_click() {
    uint8_t v = SPK_VOL_TABLE[g_spk_vol_idx];
    if (v == 0) return;
    M5Cardputer.Speaker.setVolume(v);
    M5Cardputer.Speaker.tone(4200, 30);
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

        // fn toggles between content screens and the COMMANDS screen
        if (ks.fn) {
            m5_ui_click();
            if (g_cli_active) { g_cli_active = false; g_cli_input = ""; }
            g_sb_sel_active = false;
            g_sb_dropdown   = false;
            if (g_screen == M5Screen::MENU || g_screen == M5Screen::SETTINGS) enter_can();
            else                                                              enter_menu();
            return;
        }

        // opt toggles status bar selection mode
        if (ks.opt) {
            if (g_sb_sel_active) {
                bool was_dropdown = g_sb_dropdown;
                g_sb_sel_active   = false;
                g_sb_dropdown     = false;
                draw_status_bar();
                if (was_dropdown) {
                    if      (g_screen == M5Screen::SETTINGS) draw_settings();
                    else if (g_screen == M5Screen::MENU)     draw_menu();
                    else if (g_screen == M5Screen::FEED)     redraw_feed();
                    else                                     redraw_can();
                }
            } else {
                if (g_cli_active) { g_cli_active = false; draw_cli_bar(); }
                g_sb_sel_active = true;
                g_sb_sel        = SB_ITEM_FEEDMENU;
                draw_status_bar();
            }
            return;
        }

        // ctrl alone toggles CLI bar (CAN/FEED screens only, not in status bar selection)
        if (ks.ctrl && ks.word.empty() && g_screen != M5Screen::MENU && g_screen != M5Screen::SETTINGS && !g_sb_sel_active) {
            m5_ui_click();
            g_cli_active = !g_cli_active;
            g_cli_input  = "";
            draw_cli_bar();
            return;
        }

        if (g_sb_sel_active) { handle_sb_sel_keys(ks); return; }

        if      (g_screen == M5Screen::SETTINGS) handle_settings_keys(ks);
        else if (g_screen == M5Screen::MENU)     handle_menu_keys(ks);
        else                                     handle_feed_keys(ks);
    }

    if (g_relay_mirror != g_last_mirror) {
        g_last_mirror = g_relay_mirror;
        draw_status_bar();
        if (!g_sb_dropdown) {
            if      (g_screen == M5Screen::MENU)     draw_menu();
            else if (g_screen == M5Screen::SETTINGS) draw_settings();
        }
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

    uint16_t color = LIGHTGREY;
    if      (strcmp(f.source, "wifi") == 0) color = GREEN;
    else if (strcmp(f.source, "self") == 0) color = CYAN;

    // Raw hex → CAN screen
    char buf[64];
    int pos = snprintf(buf, sizeof(buf), "[%s] %03X", f.source, f.id);
    for (uint8_t i = 0; i < f.dlc && i < 8; i++)
        pos += snprintf(buf + pos, sizeof(buf) - pos, " %02X", f.data[i]);
    print_can(String(buf), color);

    // Human-readable → FEED screen
    char hbuf[LINE_BUF_SZ];
    if (decode_frame(f, hbuf, sizeof(hbuf)))
        print_feed(String(hbuf), color);
}

// --- Screen transitions ---

static void enter_can() {
    g_screen = M5Screen::CAN;
    draw_status_bar();
    redraw_can();
    draw_cli_bar();
}

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

static void enter_settings() {
    g_screen = M5Screen::SETTINGS;
    draw_status_bar();
    draw_settings();
    draw_cli_bar();
}

// --- Status bar ---

static void draw_status_bar() {
    int cx = M5Cardputer.Display.getCursorX();
    int cy = M5Cardputer.Display.getCursorY();

    M5Cardputer.Display.fillRect(0, 0, SCREEN_W, STATUS_BAR_H, ACAPULCO_BLUE);
    M5Cardputer.Display.drawFastHLine(0, STATUS_BAR_H, SCREEN_W, DARKGREY);

    const int LABEL_W = 56;
    bool feedmenu_sel = g_sb_sel_active && (g_sb_sel == SB_ITEM_FEEDMENU);
    if (feedmenu_sel) {
        M5Cardputer.Display.fillRect(2, 4, LABEL_W, 14, DARKGREY);
        M5Cardputer.Display.drawRect(2, 4, LABEL_W, 14, WHITE);
    }
    const char* label =
        g_screen == M5Screen::SETTINGS ? "SETTINGS" :
        g_screen == M5Screen::MENU     ? "COMMANDS" :
        g_screen == M5Screen::FEED     ? "FEED"     : "CAN";
    int label_px = strlen(label) * 6;
    M5Cardputer.Display.setCursor(2 + (LABEL_W - label_px) / 2, (STATUS_BAR_H - 8) / 2);
    M5Cardputer.Display.setTextColor(WHITE);
    M5Cardputer.Display.print(label);

    int startX = 64, boxW = 14, spacing = 16;
    for (int i = 0; i < 6; i++) {
        bool     on  = (g_relay_mirror >> i) & 1;
        bool     sel = g_sb_sel_active && (g_sb_sel == i);
        int      x   = startX + i * spacing;
        uint16_t bg  = sel ? (uint16_t)DARKGREY :
                       on  ? (uint16_t)GREEN :
                       g_sb_sel_active ? (uint16_t)ACAPULCO_BLUE : (uint16_t)DARKGREY;
        M5Cardputer.Display.fillRect(x, 4, boxW, 14, bg);
        M5Cardputer.Display.drawRect(x, 4, boxW, 14, WHITE);
        M5Cardputer.Display.setCursor(x + (boxW - 6) / 2, 7);
        M5Cardputer.Display.setTextColor(sel ? WHITE : (on ? BLACK : WHITE));
        M5Cardputer.Display.print(String(i + 1));
    }

    // Battery
    if (g_batt_pct >= 0) {
        int bx = 182, by = 4, bw = 30, bh = 14;
        // 0x0400 ≈ #008200 — dark green with enough contrast for white text
        uint16_t col = g_batt_pct > 20 ? (uint16_t)0x0400 : (g_batt_pct > 10 ? (uint16_t)YELLOW : (uint16_t)RED);
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

    // Peer count circle
    uint8_t peers = bus_peer_count();
    char nbuf[4];
    snprintf(nbuf, sizeof(nbuf), "%d", peers);
    int cr      = 7;
    int cx_peer = SCREEN_W - cr - 2;
    int cy_peer = STATUS_BAR_H / 2;
    M5Cardputer.Display.fillCircle(cx_peer, cy_peer, cr, peers > 0 ? (uint16_t)0x0400 : (uint16_t)RED);
    M5Cardputer.Display.drawCircle(cx_peer, cy_peer, cr, WHITE);
    M5Cardputer.Display.setCursor(cx_peer - (int)(strlen(nbuf) * 3), cy_peer - 4);
    M5Cardputer.Display.setTextColor(WHITE);
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

// --- Status bar dropdown ---

static void draw_sb_dropdown() {
    const int dx = 2, dy = STATUS_BAR_H + 1, dw = 60, dih = 12;
    int dh = SB_DD_COUNT * dih + 2;
    M5Cardputer.Display.fillRect(dx, dy, dw, dh, BLACK);
    M5Cardputer.Display.drawRect(dx, dy, dw, dh, DARKGREY);
    for (int i = 0; i < SB_DD_COUNT; i++) {
        int  iy  = dy + 1 + i * dih;
        bool sel = (i == g_sb_dd_sel);
        if (sel) M5Cardputer.Display.fillRect(dx + 1, iy, dw - 2, dih, DARKGREY);
        M5Cardputer.Display.setCursor(dx + 4, iy + 2);
        M5Cardputer.Display.setTextColor(sel ? YELLOW : WHITE);
        M5Cardputer.Display.print(SB_DROPDOWN_ITEMS[i]);
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

// --- Settings screen ---

static void draw_settings() {
    M5Cardputer.Display.fillRect(0, STATUS_BAR_H + 1, SCREEN_W,
                                 CLI_BAR_Y - STATUS_BAR_H - 1, BLACK);

    struct Row { const char* label; const char* val; };
    char spk[16];
    snprintf(spk, sizeof(spk), "%s", SPK_VOL_LABELS[g_spk_vol_idx]);
    Row rows[SET_COUNT] = {
        {"BT (relay node)", g_bt_enabled   ? "ON"  : "OFF"},
        {"WiFi (all nodes)", g_wifi_enabled ? "ON"  : "OFF"},
        {"Speaker volume",  spk},
    };

    for (int i = 0; i < SET_COUNT; i++) {
        int  y   = MENU_START_Y + i * (MENU_ITEM_H + 4);
        bool sel = (i == g_set_sel);
        if (sel) M5Cardputer.Display.fillRect(0, y, SCREEN_W, MENU_ITEM_H + 2, DARKGREY);
        M5Cardputer.Display.setCursor(4, y + 2);
        M5Cardputer.Display.setTextColor(sel ? YELLOW : WHITE);
        M5Cardputer.Display.print(rows[i].label);

        int vw = strlen(rows[i].val) * 6;
        M5Cardputer.Display.setCursor(SCREEN_W - vw - 6, y + 2);
        M5Cardputer.Display.setTextColor(sel ? CYAN : LIGHTGREY);
        M5Cardputer.Display.print(rows[i].val);
    }

    int help_y = MENU_START_Y + SET_COUNT * (MENU_ITEM_H + 4) + 4;
    M5Cardputer.Display.setCursor(4, help_y);
    M5Cardputer.Display.setTextColor(DARKGREY);
    M5Cardputer.Display.print(";/.: nav  enter: change  del: back");
}

static void apply_setting(int item) {
    uint8_t d[8] = {};
    switch (item) {
        case SET_ITEM_BT: {
            g_bt_enabled = !g_bt_enabled;
            d[0] = CFG_TARGET_RELAY_CTRL;
            d[1] = CFG_KEY_BT_ENABLED;
            d[2] = 0;
            d[4] = g_bt_enabled ? 1 : 0;
            bus_tx(CAN_ID_CONFIG_WRITE, d, 8);
            m5_set_event(g_bt_enabled ? "BT enabled" : "BT disabled");
            break;
        }
        case SET_ITEM_WIFI: {
            g_wifi_enabled = !g_wifi_enabled;
            d[0] = CFG_TARGET_BROADCAST;
            d[1] = CFG_KEY_WIFI_ENABLED;
            d[2] = 0;
            d[4] = g_wifi_enabled ? 1 : 0;
            bus_tx(CAN_ID_CONFIG_WRITE, d, 8);
            m5_set_event(g_wifi_enabled ? "WiFi enabled" : "WiFi disabled");
            break;
        }
        case SET_ITEM_SPK: {
            g_spk_vol_idx = (g_spk_vol_idx + 1) % SPK_VOL_COUNT;
            g_prefs.begin("m5card", false);
            g_prefs.putUChar("spk_vol", g_spk_vol_idx);
            g_prefs.end();
            char buf[24];
            snprintf(buf, sizeof(buf), "Speaker: %s", SPK_VOL_LABELS[g_spk_vol_idx]);
            m5_set_event(buf);
            break;
        }
    }
}

static void handle_settings_keys(const Keyboard_Class::KeysState& ks) {
    if (ks.del) { enter_can(); return; }

    if (!ks.word.empty()) {
        char c = ks.word[0];
        if (c == ';') {
            m5_ui_click();
            g_set_sel = (g_set_sel - 1 + SET_COUNT) % SET_COUNT;
            draw_settings();
            return;
        }
        if (c == '.') {
            m5_ui_click();
            g_set_sel = (g_set_sel + 1) % SET_COUNT;
            draw_settings();
            return;
        }
        if (c >= '1' && c <= '6') {
            m5_ui_click();
            uint8_t relay = c - '1';
            bool    on    = (g_relay_mirror >> relay) & 1;
            uint8_t mask  = 1 << relay;
            uint8_t d[2]  = {mask, on ? (uint8_t)0x00 : mask};
            bus_tx(CAN_ID_RELAY_CMD, d, 2);
            return;
        }
    }

    if (ks.enter) {
        m5_ui_click();
        apply_setting(g_set_sel);
        draw_settings();
    }
}

// --- CAN screen (raw hex log) ---

static void redraw_can() {
    M5Cardputer.Display.fillRect(0, FEED_Y, SCREEN_W, FEED_H, BLACK);
    uint8_t start = (g_can_count < MAX_LINES) ? 0 : g_can_head;
    uint8_t n     = (g_can_count < MAX_LINES) ? g_can_count : MAX_LINES;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (start + i) % MAX_LINES;
        M5Cardputer.Display.setCursor(1, FEED_Y + i * LINE_H + 1);
        M5Cardputer.Display.setTextColor(g_can_lines[idx].color);
        M5Cardputer.Display.print(g_can_lines[idx].text);
    }
}

static void print_can(const String& text, uint16_t color) {
    LogLine& line = g_can_lines[g_can_head];
    strncpy(line.text, text.c_str(), LINE_BUF_SZ - 1);
    line.text[LINE_BUF_SZ - 1] = '\0';
    line.color  = color;
    g_can_head  = (g_can_head + 1) % MAX_LINES;
    if (g_can_count < MAX_LINES) g_can_count++;
    if (g_screen == M5Screen::CAN) redraw_can();
}

// --- FEED screen (human-readable log) ---

static bool decode_frame(const BusFrame& f, char* buf, size_t sz) {
    switch (f.id) {
        case CAN_ID_NODE_ANNOUNCE:
            if (f.dlc >= 1) {
                snprintf(buf, sz, "Node 0x%02X online (%d peers)", f.data[0], f.dlc >= 2 ? f.data[1] : 0);
                return true;
            }
            break;
        case CAN_ID_BOOT_EVENT:
            if (f.dlc >= 1) { snprintf(buf, sz, "Node 0x%02X booted", f.data[0]); return true; }
            break;
        case CAN_ID_BUS_ERROR:
            if (f.dlc >= 2) {
                const char* err = f.data[1] == 0 ? "recovered" :
                                  f.data[1] == 1 ? "BUS_OFF" :
                                  f.data[1] == 2 ? "ERROR_PASSIVE" :
                                  f.data[1] == 3 ? "TX_FAIL" : "RX_OVERFLOW";
                snprintf(buf, sz, "CAN 0x%02X %s", f.data[0], err);
                return true;
            }
            break;
        case CAN_ID_REBOOT_CMD:
            if (f.dlc >= 1) {
                if (f.data[0] == 0xFF) snprintf(buf, sz, "Reboot: all nodes");
                else                   snprintf(buf, sz, "Reboot: node 0x%02X", f.data[0]);
                return true;
            }
            break;
        case CAN_ID_RELAY_CMD:
            if (f.dlc >= 2) {
                uint8_t mask = f.data[0], state = f.data[1];
                if (mask == 0x3F && state == 0x00) {
                    snprintf(buf, sz, "All relays OFF");
                } else {
                    int n = 0;
                    for (int i = 0; i < 6; i++)
                        if (mask & (1u << i))
                            n += snprintf(buf + n, sz - n, "%sR%d %s",
                                          n ? " " : "", i + 1, (state & (1u << i)) ? "ON" : "OFF");
                }
                return true;
            }
            break;
        case CAN_ID_RELAY_STATUS:
            if (f.dlc >= 1) {
                uint8_t bm = f.data[0];
                if (bm == 0) { snprintf(buf, sz, "Relays: all OFF"); }
                else {
                    int n = snprintf(buf, sz, "Relays ON:");
                    for (int i = 0; i < 6; i++)
                        if (bm & (1u << i))
                            n += snprintf(buf + n, sz - n, " R%d", i + 1);
                }
                return true;
            }
            break;
        case CAN_ID_SWITCH_EVENT:
            if (f.dlc >= 2) {
                const char* ev = f.data[1] == 1 ? "pressed" :
                                 f.data[1] == 2 ? "long press" :
                                 f.data[1] == 3 ? "double tap" : "released";
                snprintf(buf, sz, "SW%d %s", f.data[0] + 1, ev);
                return true;
            }
            break;
        case CAN_ID_ENCODER_EVENT:
            if (f.dlc >= 1) {
                const char* ev = f.data[0] == 0 ? "CW" :
                                 f.data[0] == 1 ? "CCW" :
                                 f.data[0] == 2 ? "press" :
                                 f.data[0] == 3 ? "release" : "long";
                if (f.data[0] <= 1 && f.dlc >= 2)
                    snprintf(buf, sz, "Encoder %s x%d", ev, f.data[1]);
                else
                    snprintf(buf, sz, "Encoder %s", ev);
                return true;
            }
            break;
        case CAN_ID_TELEMETRY:
            if (f.dlc >= 2) {
                uint16_t cv = f.data[0] | ((uint16_t)f.data[1] << 8);
                int n = snprintf(buf, sz, "Batt: %d.%02dV", cv / 100, cv % 100);
                if (f.dlc >= 6) {
                    uint16_t cv2 = f.data[4] | ((uint16_t)f.data[5] << 8);
                    snprintf(buf + n, sz - n, " / %d.%02dV", cv2 / 100, cv2 % 100);
                }
                return true;
            }
            break;
        case CAN_ID_ENGINE_DATA:
            if (f.dlc >= 2) {
                uint16_t rpm = f.data[0] | ((uint16_t)f.data[1] << 8);
                snprintf(buf, sz, "RPM: %u", rpm);
                return true;
            }
            break;
        case CAN_ID_GPS_DATA:
            if (f.dlc >= 4) {
                uint16_t spd = f.data[0] | ((uint16_t)f.data[1] << 8);
                uint16_t hdg = f.data[2] | ((uint16_t)f.data[3] << 8);
                snprintf(buf, sz, "GPS %d.%d mph %d.%d\xB0", spd / 10, spd % 10, hdg / 10, hdg % 10);
                return true;
            }
            break;
        case CAN_ID_WBO2_DATA:
            if (f.dlc >= 2) {
                uint16_t a = f.data[0] | ((uint16_t)f.data[1] << 8);
                snprintf(buf, sz, "AFR: %d.%02d", a / 100, a % 100);
                return true;
            }
            break;
        case CAN_ID_ECU_DATA:
            if (f.dlc >= 3) {
                snprintf(buf, sz, "ECU MAP:%dkPa TPS:%d%%", f.data[1], f.data[2]);
                return true;
            }
            break;
        case CAN_ID_VIPER_CMD:
            if (f.dlc >= 1) {
                const char* cmd = f.data[0] == 1 ? "Lock" :
                                  f.data[0] == 2 ? "Unlock" : "Remote Start";
                snprintf(buf, sz, "Viper: %s", cmd);
                return true;
            }
            break;
        case CAN_ID_CONFIG_WRITE:
            if (f.dlc >= 5) {
                snprintf(buf, sz, "Config 0x%02X key=0x%02X val=%u",
                         f.data[0], f.data[1], f.data[4]);
                return true;
            }
            break;
    }
    return false;
}

static void redraw_feed() {
    M5Cardputer.Display.fillRect(0, FEED_Y, SCREEN_W, FEED_H, BLACK);
    uint8_t start = (g_feed_count < MAX_LINES) ? 0 : g_feed_head;
    uint8_t n     = (g_feed_count < MAX_LINES) ? g_feed_count : MAX_LINES;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (start + i) % MAX_LINES;
        M5Cardputer.Display.setCursor(1, FEED_Y + i * LINE_H + 1);
        M5Cardputer.Display.setTextColor(g_feed_lines[idx].color);
        M5Cardputer.Display.print(g_feed_lines[idx].text);
    }
}

static void print_feed(const String& text, uint16_t color) {
    LogLine& line = g_feed_lines[g_feed_head];
    strncpy(line.text, text.c_str(), LINE_BUF_SZ - 1);
    line.text[LINE_BUF_SZ - 1] = '\0';
    line.color   = color;
    g_feed_head  = (g_feed_head + 1) % MAX_LINES;
    if (g_feed_count < MAX_LINES) g_feed_count++;
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

// --- Status bar selection key handling ---

static void handle_sb_sel_keys(const Keyboard_Class::KeysState& ks) {
    if (g_sb_dropdown) {
        if (ks.del) {
            g_sb_dropdown = false;
            if      (g_screen == M5Screen::MENU) draw_menu();
            else if (g_screen == M5Screen::FEED) redraw_feed();
            else                                 redraw_can();
            return;
        }
        if (!ks.word.empty()) {
            char c = ks.word[0];
            if (c == ';') { m5_ui_click(); g_sb_dd_sel = (int8_t)((g_sb_dd_sel - 1 + SB_DD_COUNT) % SB_DD_COUNT); draw_sb_dropdown(); return; }
            if (c == '.') { m5_ui_click(); g_sb_dd_sel = (int8_t)((g_sb_dd_sel + 1) % SB_DD_COUNT); draw_sb_dropdown(); return; }
        }
        if (ks.enter) {
            m5_ui_click();
            g_sb_dropdown   = false;
            g_sb_sel_active = false;
            if      (g_sb_dd_sel == 3) enter_settings();
            else if (g_sb_dd_sel == 2) enter_menu();
            else if (g_sb_dd_sel == 1) enter_feed();
            else                       enter_can();
        }
        return;
    }

    // Navigate between status bar items
    if (!ks.word.empty()) {
        char c = ks.word[0];
        if (c == ';') { m5_ui_click(); g_sb_sel = (int8_t)((g_sb_sel - 1 + SB_ITEMS) % SB_ITEMS); draw_status_bar(); return; }
        if (c == '.') { m5_ui_click(); g_sb_sel = (int8_t)((g_sb_sel + 1) % SB_ITEMS);             draw_status_bar(); return; }
    }

    if (ks.del) {
        g_sb_sel_active = false;
        draw_status_bar();
        return;
    }

    bool activate = ks.enter || (!ks.word.empty() && ks.word[0] == ' ');
    if (activate) {
        if (g_sb_sel < SB_ITEM_FEEDMENU) {
            m5_ui_click();
            uint8_t relay = (uint8_t)g_sb_sel;
            bool    on    = (g_relay_mirror >> relay) & 1;
            uint8_t mask  = 1u << relay;
            uint8_t d[2]  = {mask, on ? (uint8_t)0x00 : mask};
            bus_tx(CAN_ID_RELAY_CMD, d, 2);
            print_can("[relay " + String(relay + 1) + (on ? " OFF]" : " ON]"), ORANGE);
        } else {
            g_sb_dropdown = true;
            g_sb_dd_sel   = g_screen == M5Screen::SETTINGS ? 3 :
                            g_screen == M5Screen::MENU     ? 2 :
                            g_screen == M5Screen::FEED     ? 1 : 0;
            draw_sb_dropdown();
        }
    }
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
                if (g_screen == M5Screen::FEED) {
                    g_feed_head = 0; g_feed_count = 0; redraw_feed();
                } else {
                    g_can_head = 0; g_can_count = 0; redraw_can();
                }
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

    m5_ui_click();
    uint8_t relay = c - '1';
    bool    on    = (g_relay_mirror >> relay) & 1;
    uint8_t mask  = 1 << relay;
    uint8_t d[2]  = {mask, on ? (uint8_t)0x00 : mask};
    bus_tx(CAN_ID_RELAY_CMD, d, 2);
    print_can("[relay " + String(relay + 1) + (on ? " OFF]" : " ON]"), ORANGE);
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

    if (!id_parsed) { print_can("[cli] bad frame", RED); return; }
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
    if (ks.del) { enter_can(); return; }

    if (!ks.word.empty()) {
        char c = ks.word[0];

        if (c == ';') {  // up arrow
            m5_ui_click();
            g_menu_sel = (g_menu_sel - 1 + MENU_COUNT) % MENU_COUNT;
            if (g_menu_sel < g_menu_scroll)
                g_menu_scroll = g_menu_sel;
            else if (g_menu_sel == MENU_COUNT - 1)
                g_menu_scroll = max(0, MENU_COUNT - VISIBLE_MENU_ITEMS);
            draw_menu();
            return;
        }
        if (c == '.') {  // down arrow
            m5_ui_click();
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
            m5_ui_click();
            uint8_t relay = c - '1';
            bool    on    = (g_relay_mirror >> relay) & 1;
            uint8_t mask  = 1 << relay;
            uint8_t d[2]  = {mask, on ? (uint8_t)0x00 : mask};
            bus_tx(CAN_ID_RELAY_CMD, d, 2);
            return;
        }
    }

    if (ks.enter) {
        m5_ui_click();
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
    if (!SD.begin(SS, SPI, 25000000)) { print_can("[SD] mount failed", RED); return; }

    char path[24];
    for (int i = 1; i <= 999; i++) {
        snprintf(path, sizeof(path), "/canlog_%03d.csv", i);
        if (!SD.exists(path)) break;
    }

    g_log_file = SD.open(path, FILE_WRITE);
    if (!g_log_file) { print_can("[SD] open failed", RED); return; }

    g_log_file.println("ms,source,id,dlc,d0,d1,d2,d3,d4,d5,d6,d7");
    g_logging = true;
    print_can(String("[SD] ") + path, GREEN);
    draw_status_bar();
}

static void log_stop() {
    if (!g_logging) return;
    g_log_file.flush();
    g_log_file.close();
    g_logging = false;
    print_can("[SD] log stopped", YELLOW);
    draw_status_bar();
}
#else
static void log_start() { print_can("[SD] add ENABLE_SD_LOG to enable", RED); }
static void log_stop()  {}
#endif

#endif // ENABLE_M5_CARDPUTER
