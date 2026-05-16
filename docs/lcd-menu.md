# CANoE — LCD Menu & Display Guide

The switch panel (and optionally the viper interface) has a built-in menu driven by the rotary encoder and BTN4 (GPIO 14). This guide covers navigation, the idle display format, and how to customize relay labels and icons.

---

## Table of Contents

1. [Navigation](#1-navigation)
2. [Menu Structure](#2-menu-structure)
3. [LCD Display Layout](#3-lcd-display-layout)
4. [Relay Labels and Custom Icons](#4-relay-labels-and-custom-icons)

---

## 1. Navigation

| Input | Action |
|-------|--------|
| Long-press BTN4 from idle (≥ 600 ms) | Enter menu |
| Short-press BTN4 in menu | Navigate: enter submenu, or confirm "← Back" / "← Exit" |
| Long-press BTN4 on an action item | Execute / toggle (relay, Viper command, backlight) |
| Rotate encoder CW | Scroll down |
| Rotate encoder CCW | Scroll up |
| 15 s no input | Auto-exit back to status display |

**"← Exit"** (top level) and **"← Back"** (submenus) are always the **last** item in their list — scroll past the content items to reach them. Action items require a **long-press** to execute; short-press is navigation only.

---

## 2. Menu Structure

```
[idle: status row + last event]
│
└─ MENU  (long-press BTN4)
   ├─ Relays
   │   ├─ Relay 1–6    long-press → toggle ON/OFF
   │   └─ ← Back
   ├─ Viper
   │   ├─ Lock / Arm       long-press → send command
   │   ├─ Unlock/Disarm    long-press → send command
   │   ├─ Remote Start     long-press → send command
   │   └─ ← Back
   ├─ Bus Status       live TWAI error counters (read-only)
   │   └─ ← Back
   ├─ Display
   │   ├─ Backlight    long-press → toggle ON/OFF
   │   └─ ← Back
   ├─ WiFi             enable/disable WiFi per node
   │   ├─ SwitchPnl   long-press → toggle (self: restarts immediately)
   │   ├─ RelayCtr    long-press → toggle (sends CONFIG_WRITE, node restarts)
   │   ├─ Viper       long-press → toggle
   │   └─ ← Back
   └─ ← Exit
```

Each submenu is compiled in independently via `MENU_HAS_*` flags in the node config. Omit a flag and that submenu won't appear.

| Flag | Submenu |
|------|---------|
| `MENU_HAS_RELAYS` | Relay toggle submenu |
| `MENU_HAS_VIPER` | Viper lock/unlock/start |
| `MENU_HAS_BUS` | Live TWAI health counters |
| `MENU_HAS_DISPLAY` | LCD backlight toggle |
| `MENU_HAS_WIFI` | Per-node WiFi enable/disable |
| `MENU_HAS_TX_MODE` | CAN/WiFi transport mode selector |
| `MENU_HAS_BEEP` | Buzzer mute + startup sound toggles |
| `MENU_HAS_REBOOT` | Reboot any node by ID |
| `MENU_HAS_BLUETOOTH` | BLE advertising (discoverability) + power toggle |

---

## 3. LCD Display Layout

### Idle (Status Display)

```
C✦N✦A✦    ✦-----   ← row 0: transport indicators + relay bitmap
Headlights ON        ← row 1: last event
```

**Row 0 indicators (cols 0–5):**

| Chars | Label | Meaning |
|-------|-------|---------|
| `C✦` / `C-` | CAN | Wired CAN bus healthy (TX or RX seen in last 5 s) |
| `N✦` / `N-` | ESP-NOW | A peer frame received via ESP-NOW in the last 5 s |
| `A✦` / `A-` | AP | WiFi SoftAP active on this node |

Filled icon (✦) = OK, hollow (-) = down. Cols 6–9 are a spacer. Cols 10–15 are the 6-relay bitmap — one character per relay, defined by `RELAY_n_ICON_ON/OFF`.

**Row 1** shows the last meaningful event text (relay state change, switch press, etc.). `lcd_set_event()` automatically truncates text to the first widget boundary so widgets on row 1 can coexist without overlap.

### In Menu

**Top-level navigation:**
```
MENU  [2/6]
→ Viper
```

**Inside Relays submenu:**
```
Relays ✦-----
→ ✦ Headlights ON    ← long-press to toggle
```

**Back item selected:**
```
Relays ✦-----
← Back               ← short-press to go back
```

---

## 4. Relay Labels and Custom Icons

Each relay can have a human-readable label and custom HD44780 CGRAM icons for its ON and OFF states. These are defined at compile time in the node config header.

```cpp
// firmware/configs/switch_panel.h
#define RELAY_1_LABEL    "Headlights"
#define RELAY_1_ICON_ON  {0b10101, 0b10101, 0b10101, 0b00000, 0b11111, 0b11111, 0b01110, 0b00000}
#define RELAY_1_ICON_OFF {0b00000, 0b00000, 0b00000, 0b11111, 0b10001, 0b11111, 0b01110, 0b00000}

#define RELAY_2_LABEL    "Fuel Pump"
// (no ICON_ON/OFF → uses default full-block / dash chars)

#define RELAY_3_LABEL    "Choke"
```

Each icon is an 8-byte HD44780 5×8 pixel bitmap. Omit the `ICON_ON`/`ICON_OFF` macros for a relay to use the defaults: ON = full block (`█`), OFF = dash (`-`).

**CGRAM limits:**

The HD44780 has 8 CGRAM slots:
- Slot 0 is **reserved** — it maps to the C null terminator (`\x00`), which silently truncates `snprintf` output. Never use it.
- 2 slots are consumed by the shared CAN/WiFi status icons (filled / hollow glyphs used by all three row-0 indicators).
- That leaves **5 slots** for relay icons total across all relays.

Defining more than 5 `RELAY_n_ICON_ON/OFF` macros silently skips the overflow — no error, just missing icons. Default block/dash chars are used instead.

**Where labels appear:**
- LCD menu Relays submenu
- LCD idle relay bitmap (using icons)
- Web UI relay tiles on the relay controller node
