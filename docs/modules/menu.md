# Module: mod_menu

Encoder-driven LCD menu system. Provides relay toggle, Viper alarm control, bus health display, backlight toggle, and per-node WiFi enable/disable submenus. Requires `ENABLE_LCD`.

## Enable

```cpp
#define ENABLE_MENU
// also requires:
#define ENABLE_LCD
```

---

## Config defines

| Define | Description |
|--------|-------------|
| `MENU_HAS_RELAYS` | Compile in relay toggle submenu |
| `MENU_HAS_VIPER` | Compile in Viper lock/unlock/start submenu |
| `MENU_HAS_BUS` | Compile in live TWAI health counter display |
| `MENU_HAS_DISPLAY` | Compile in backlight toggle |
| `MENU_HAS_WIFI` | Compile in per-node WiFi enable/disable |

Omit any flag and that submenu is not included.

---

## CAN frames

The menu itself sends no CAN frames. Actions dispatched from the menu (relay toggle, Viper commands, WiFi config writes) use `bus_tx()` through the relevant modules.

### Indirectly sends (via actions)

| ID | Name | When |
|----|------|------|
| `0x100` | `RELAY_CMD` | Relay toggle in Relays submenu |
| `0x510` | `VIPER_CMD` | Lock/unlock/start in Viper submenu |
| `0x400` | `CONFIG_WRITE` | WiFi enable/disable in WiFi submenu |

---

## Navigation

| Input | Action |
|-------|--------|
| Long-press menu button (≥ 600 ms) | Enter menu from idle |
| Short-press in menu | Navigate: enter submenu, confirm "← Back" / "← Exit" |
| Long-press on action item | Execute / toggle |
| Rotate encoder CW | Scroll down |
| Rotate encoder CCW | Scroll up |
| 15 s no input | Auto-exit back to status display |

The menu button is driven by rules — wire `ACT_MENU_SELECT()` to a short-press and `ACT_MENU_ENTER()` to a long-press of the same button index:

```cpp
RULE(TRIG_SW_PRESS(6),  ACT_MENU_SELECT()),
RULE(TRIG_SW_LONG(6),   ACT_MENU_ENTER()),
```

Encoder scroll events are handled directly in `accessory_node.ino`'s `bus_rx()` loop (self-echoed ENCODER_EVENT frames call `menu_scroll()`).

---

## Menu structure

```
[idle display]
└─ MENU  (long-press menu button)
   ├─ Relays           (MENU_HAS_RELAYS)
   │   ├─ Relay 1–N   long-press → toggle
   │   └─ ← Back
   ├─ Viper            (MENU_HAS_VIPER)
   │   ├─ Lock/Arm     long-press → VIPER_CMD 0x01
   │   ├─ Unlock       long-press → VIPER_CMD 0x02
   │   ├─ Remote Start long-press → VIPER_CMD 0x03
   │   └─ ← Back
   ├─ Bus Status       (MENU_HAS_BUS) — read-only TWAI counters
   │   └─ ← Back
   ├─ Display          (MENU_HAS_DISPLAY)
   │   ├─ Backlight    long-press → toggle
   │   └─ ← Back
   ├─ WiFi             (MENU_HAS_WIFI)
   │   ├─ <NodeName>   long-press → CONFIG_WRITE toggle (self: restarts)
   │   └─ ← Back
   └─ ← Exit
```

---

## Integration notes

- `menu_setup()` must be called after `lcd_setup()` and `relay_icons_init()` (if relays are present) so relay labels are available.
- `menu_tick()` must be called every `loop()` to handle auto-exit timeout.
- The WiFi submenu sends `CONFIG_WRITE` with key `0x31` (AP enable) to other nodes, which cause them to restart. When toggling WiFi on the local node, `menu_action()` saves to NVS and immediately restarts — the menu will not return to idle.
- Relay state shown in the Relays submenu comes from `g_relay_mirror` (updated by relay status frames), not a direct GPIO read.
