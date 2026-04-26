// mod_menu.cpp — LCD menu system.
//
// Enabled by ENABLE_MENU in node_config.h.
// Individual submenus gated by MENU_HAS_RELAYS / VIPER / BUS / DISPLAY / WIFI.

#include <Arduino.h>
#include <Preferences.h>
#include "driver/twai.h"
#include "node_config.h"

#ifdef ENABLE_MENU

#include "can_protocol.h"
#include "bus.h"
#include "node_state.h"
#include "mod_lcd.h"
#include "mod_relay.h"
#include "mod_buzzer.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

// --------------------------------------------------------------
// Menu section IDs — stable identifiers regardless of which items
// are compiled in. g_items[] is built at setup time from active flags.
// --------------------------------------------------------------
#define MENU_ID_EXIT    0
#define MENU_ID_RELAYS  1
#define MENU_ID_VIPER   2
#define MENU_ID_BUS     3
#define MENU_ID_DISPLAY 4
#define MENU_ID_WIFI    5
#define MENU_ID_TX_MODE 6

struct MenuItem {
  uint8_t     id;
  const char* label;
  uint8_t     sub_count;  // 0 = no sub-level; else includes "< Back" at index 0
};

#define MENU_TIMEOUT_MS 15000

#ifdef MENU_HAS_VIPER
static const char* const VIPER_LABELS[] = { "Lock / Arm", "Unlock/Disarm", "Remote Start" };
static const uint8_t     VIPER_CMDS[]   = { VIPER_CMD_LOCK, VIPER_CMD_UNLOCK, VIPER_CMD_REMOTE_START };
#endif

#ifdef MENU_HAS_WIFI
// Node WiFi state mirror: index 0 = self (switch panel), 1 = relay ctrl, 2 = viper
static bool g_node_wifi[3] = { true, false, true };
#endif

static MenuItem g_items[9];  // max 8 sections + spare
static uint8_t  g_item_count = 0;

static uint8_t  g_level    = 0;
static uint8_t  g_top_sel  = 0;
static uint8_t  g_sub_sel  = 0;
static uint32_t g_last_act = 0;

// --------------------------------------------------------------
// Local relay toggle helper (menu doesn't depend on mod_switches)
// --------------------------------------------------------------
static void relay_toggle(uint8_t relay_idx) {
  if (relay_idx >= 8) return;
  uint8_t mask = 1 << relay_idx;
  uint8_t want = (g_relay_mirror & mask) ? 0 : mask;
  uint8_t d[2] = { mask, want };
  bus_tx(CAN_ID_RELAY_CMD, d, 2);
  g_relay_mirror = (g_relay_mirror & ~mask) | want;  // update immediately for menu_draw()
}

// \x7E = → (right arrow, HD44780 ROM A00) — selection cursor
// \x7F = ← (left arrow, HD44780 ROM A00) — back indicator
// \xFF = full block                        — relay ON
// '-'                                      — relay OFF

// --------------------------------------------------------------
// Draw
// --------------------------------------------------------------
static void menu_draw() {
  char r0[LCD_COLS + 1], r1[LCD_COLS + 1];
  uint8_t id         = g_items[g_top_sel].id;
  uint8_t back_idx   = g_items[g_top_sel].sub_count - 1;
  bool    is_back    = (g_level > 0) && (g_sub_sel == back_idx);

  if (g_level == 0) {
    snprintf(r0, sizeof(r0), "MENU  [%u/%u]", g_top_sel + 1, g_item_count);
    snprintf(r1, sizeof(r1), "\x7E %s", g_items[g_top_sel].label);
  } else {
    switch (id) {

#ifdef MENU_HAS_RELAYS
      case MENU_ID_RELAYS: {
        // Row 0: live relay state bitmap using per-relay icons
        char bmap[7];
        for (uint8_t i = 0; i < relay_lcd_count(); i++) {
          bool on = (g_relay_mirror & (1 << i)) != 0;
          bmap[i] = lcd_relay_char(i, on);
        }
        bmap[6] = '\0';
        snprintf(r0, sizeof(r0), "Relays %s", bmap);
        if (is_back) {
          snprintf(r1, sizeof(r1), "\x7F Back");
        } else {
          bool on = (g_relay_mirror & (1 << g_sub_sel)) != 0;
          // Row 1: icon + label (up to 9 chars) + state
          snprintf(r1, sizeof(r1), "\x7E %c %-9.9s%s",
            lcd_relay_char(g_sub_sel, on),
            lcd_relay_label(g_sub_sel),
            on ? "ON " : "OFF");
        }
        break;
      }
#endif

#ifdef MENU_HAS_VIPER
      case MENU_ID_VIPER:
        if (is_back) {
          snprintf(r0, sizeof(r0), "Viper");
          snprintf(r1, sizeof(r1), "\x7F Back");
        } else {
          snprintf(r0, sizeof(r0), "Viper   [%u/3]", g_sub_sel + 1);
          snprintf(r1, sizeof(r1), "\x7E %s", VIPER_LABELS[g_sub_sel]);
        }
        break;
#endif

#ifdef MENU_HAS_BUS
      case MENU_ID_BUS: {
        twai_status_info_t info;
        if (twai_get_status_info(&info) == ESP_OK) {
          const char* st = (info.state == TWAI_STATE_RUNNING)    ? "OK " :
                           (info.state == TWAI_STATE_BUS_OFF)    ? "OFF" :
                           (info.state == TWAI_STATE_RECOVERING) ? "RCV" : "STP";
          snprintf(r0, sizeof(r0), "%s TX:%u RX:%u", st,
                   info.tx_error_counter, info.rx_error_counter);
        } else {
          snprintf(r0, sizeof(r0), "Bus Status");
        }
        snprintf(r1, sizeof(r1), "\x7F Back");
        break;
      }
#endif

#ifdef MENU_HAS_DISPLAY
      case MENU_ID_DISPLAY:
        snprintf(r0, sizeof(r0), "Display");
        if (is_back)
          snprintf(r1, sizeof(r1), "\x7F Back");
        else
          snprintf(r1, sizeof(r1), "\x7E Backlight %s", lcd_get_backlight() ? "ON " : "OFF");
        break;
#endif

#ifdef MENU_HAS_WIFI
      case MENU_ID_WIFI: {
        static const char* const NAMES[] = { "SwitchPnl", "RelayCtr ", "Viper    " };
        if (is_back) {
          snprintf(r0, sizeof(r0), "WiFi");
          snprintf(r1, sizeof(r1), "\x7F Back");
        } else {
          snprintf(r0, sizeof(r0), "WiFi    [%u/3]", g_sub_sel + 1);
          snprintf(r1, sizeof(r1), "\x7E %-9s %s", NAMES[g_sub_sel],
                   g_node_wifi[g_sub_sel] ? "ON " : "OFF");
        }
        break;
      }
#endif

#ifdef MENU_HAS_TX_MODE
      case MENU_ID_TX_MODE: {
        static const char* const MODE_LABELS[] = { "CAN+WiFi", "WiFi Only", "CAN Only" };
        BusTxMode cur = bus_get_tx_mode();
        snprintf(r0, sizeof(r0), "CAN Mode: %-8s", MODE_LABELS[(uint8_t)cur]);
        if (is_back)
          snprintf(r1, sizeof(r1), "\x7F Back");
        else
          snprintf(r1, sizeof(r1), "%s%-8s", (g_sub_sel == (uint8_t)cur ? "\x7E*" : "\x7E "),
                   MODE_LABELS[g_sub_sel]);
        break;
      }
#endif

      default:
        snprintf(r0, sizeof(r0), "Menu");
        snprintf(r1, sizeof(r1), "---");
        break;
    }
  }
  lcd_write_row(0, r0);
  lcd_write_row(1, r1);
}

// --------------------------------------------------------------
// Public API
// --------------------------------------------------------------
void menu_setup() {
  g_item_count = 0;
#ifdef MENU_HAS_RELAYS
  g_items[g_item_count++] = { MENU_ID_RELAYS,  "Relays",     (uint8_t)(relay_lcd_count() + 1) };  // N relays + Back
#endif
#ifdef MENU_HAS_VIPER
  g_items[g_item_count++] = { MENU_ID_VIPER,   "Viper",      4 };  // 3 commands + Back
#endif
#ifdef MENU_HAS_BUS
  g_items[g_item_count++] = { MENU_ID_BUS,     "Bus Status", 1 };  // Back only (status in row 0)
#endif
#ifdef MENU_HAS_DISPLAY
  g_items[g_item_count++] = { MENU_ID_DISPLAY, "Display",    2 };  // Backlight + Back
#endif
#ifdef MENU_HAS_TX_MODE
  g_items[g_item_count++] = { MENU_ID_TX_MODE, "CAN Mode",   4 };  // 3 modes + Back
#endif
#ifdef MENU_HAS_WIFI
  g_items[g_item_count++] = { MENU_ID_WIFI,    "WiFi",       4 };  // 3 nodes + Back
  Preferences p; p.begin(NVS_NAMESPACE, true);
  g_node_wifi[0] = p.getBool("wifi_en", true);
  p.end();
#endif
  g_items[g_item_count++] = { MENU_ID_EXIT,    "\x7F Exit",  0 };  // always last
}

bool menu_is_active() { return g_menu_active; }

void menu_exit() {
  g_menu_active = false;
  lcd_write_row(0, "");
  lcd_write_row(1, "");
  lcd_update_status();
  buzzer_menu_exit();
  wlogln("[menu] exit");
}

void menu_enter() {
  g_menu_active = true;
  g_level       = 0;
  g_top_sel     = 0;
  g_sub_sel     = 0;
  g_last_act    = millis();
  menu_draw();
  buzzer_menu_enter();
  wlogln("[menu] enter");
}

static void menu_back() {
  g_last_act = millis();
  if (g_level == 0) { menu_exit(); return; }
  g_level = 0; g_sub_sel = 0;
  menu_draw();
  wlogln("[menu] back");
}

void menu_scroll(int8_t dir) {
  g_last_act = millis();
  if (g_level == 0) {
    if (dir > 0) g_top_sel = (g_top_sel + 1) % g_item_count;
    else         g_top_sel = (g_top_sel + g_item_count - 1) % g_item_count;
  } else {
    uint8_t n = g_items[g_top_sel].sub_count;
    if (n == 0) { menu_back(); return; }
    if (dir > 0) g_sub_sel = (g_sub_sel + 1) % n;
    else         g_sub_sel = (g_sub_sel + n - 1) % n;
  }
  buzzer_menu_scroll();
  menu_draw();
}

void menu_select() {
  g_last_act = millis();
  if (g_level == 0) {
    if (g_items[g_top_sel].id == MENU_ID_EXIT) { menu_exit(); return; }
    g_level = 1; g_sub_sel = 0;
    buzzer_menu_select();
    menu_draw();
  } else {
    if (g_sub_sel == g_items[g_top_sel].sub_count - 1) menu_back();
    // Action items require long-press; short press is navigation only.
  }
}

void menu_action() {
  g_last_act = millis();
  if (g_level == 0) return;
  if (g_sub_sel == g_items[g_top_sel].sub_count - 1) { menu_back(); return; }
  buzzer_menu_action();
  uint8_t idx = g_sub_sel;  // direct index — Back is last, not first

  switch (g_items[g_top_sel].id) {

#ifdef MENU_HAS_RELAYS
    case MENU_ID_RELAYS:
      relay_toggle(idx);
      menu_draw();
      break;
#endif

#ifdef MENU_HAS_VIPER
    case MENU_ID_VIPER: {
      uint8_t d[1] = { VIPER_CMDS[idx] };
      bus_tx(CAN_ID_VIPER_CMD, d, 1);
      wlog("[menu] viper cmd 0x%02X\n", VIPER_CMDS[idx]);
      menu_draw();
      break;
    }
#endif

#ifdef MENU_HAS_BUS
    case MENU_ID_BUS:
      menu_back();
      break;
#endif

#ifdef MENU_HAS_DISPLAY
    case MENU_ID_DISPLAY:
      lcd_set_backlight(!lcd_get_backlight());
      menu_draw();
      break;
#endif

#ifdef MENU_HAS_WIFI
    case MENU_ID_WIFI: {
      static const uint8_t TARGETS[] = {
        CFG_TARGET_SWITCH_PANEL, CFG_TARGET_RELAY_CTRL, CFG_TARGET_VIPER
      };
      bool new_en = !g_node_wifi[idx];
      if (idx == 0) {
        // Toggle self: persist and restart.
        Preferences p; p.begin(NVS_NAMESPACE, false);
        p.putBool("wifi_en", new_en); p.end();
        wlog("[menu] wifi self -> %u, restart\n", new_en);
        delay(100); ESP.restart();
      } else {
        uint8_t d[8] = { TARGETS[idx], CFG_KEY_WIFI_ENABLED, 0, 0,
                         new_en ? 1u : 0u, 0, 0, 0x01 };
        bus_tx(CAN_ID_CONFIG_WRITE, d, 8);
        g_node_wifi[idx] = new_en;
        wlog("[menu] wifi node%u -> %u\n", idx, new_en);
        menu_draw();
      }
      break;
    }
#endif

#ifdef MENU_HAS_TX_MODE
    case MENU_ID_TX_MODE:
      bus_set_tx_mode((BusTxMode)idx);
      wlog("[menu] tx_mode=%u\n", idx);
      menu_draw();
      break;
#endif

    default: break;
  }
}

void menu_tick() {
  if (g_menu_active && (millis() - g_last_act) >= MENU_TIMEOUT_MS)
    menu_exit();
}

#endif // ENABLE_MENU
