# CANoE — Web Console Guide

Each node runs an HTTP server with a captive DNS portal on its `AccessoryBus` SoftAP. Any device on the same WiFi can reach the bus — frames are shared between nodes via ESP-NOW.

**Access:** Connect to WiFi SSID `AccessoryBus` → browser auto-opens, or navigate to `http://192.168.4.1`.

The bridge node (0x05) is also reachable from your home network at its DHCP-assigned IP. Its web UI shows an aggregated control panel covering all discovered nodes.

---

## Table of Contents

1. [CAN Frames Tab](#1-can-frames-tab)
2. [Control Tab](#2-control-tab)
3. [Rules Tab](#3-rules-tab)
4. [Network Tab](#4-network-tab)
5. [Serial Tab](#5-serial-tab)
6. [Settings (Hamburger Menu)](#6-settings-hamburger-menu)
7. [Force WiFi-Only Mode](#7-force-wifi-only-mode)

---

## 1. CAN Frames Tab

Live scrolling log of every frame on the bus, plus a hex command line and quick-action buttons.

### Raw Hex Command Line

```
<id_hex> <byte0> <byte1> ...    # hex, space-separated, up to 8 bytes
```

Examples:
```
100 01 01       relay 1 ON
100 01 00       relay 1 OFF
100 3F 00       all relays OFF
510 01          Viper lock / arm
510 02          Viper unlock / disarm
510 03          Viper remote start
308 01 01 00 00 ECU: switch to injection mode
```

### Alias Commands

All aliases start with `:`. Aliases expand to the equivalent raw frame(s).

| Alias | Action |
|-------|--------|
| `:relay <n> on\|off` | Turn relay n (1–6) on or off |
| `:alloff` | All 6 relays off |
| `:horn` | Relay 5 on — subject to 30 s safety watchdog |
| `:viper lock` | Send VIPER_CMD lock to viper_interface node |
| `:viper unlock` | Send VIPER_CMD unlock |
| `:viper start` | Send VIPER_CMD remote start |
| `:readcfg relay` | Dump all relay max-on-ms settings |
| `:save relay` | Persist relay config to NVS |
| `:reset relay` | Factory-reset relay config |
| `:cfgrelay <idx> maxon <ms> [!]` | Set per-relay safety auto-off (0 = no limit) |

The trailing `!` on `:cfgrelay` persists to NVS immediately. Without it, the change is RAM-only and lost on reboot.

---

## 2. Control Tab

Adapts to whichever node you're connected to, based on its feature flags.

### Switch Panel — Inputs and LEDs

- **Switch tiles** — reflect live physical switch state (tracked from incoming SWITCH_EVENT frames). Clicking a tile sends a SWITCH_EVENT to simulate a toggle.
- **Button tiles** — send a press/release pair with a 150 ms gap when clicked.
- **LED dots** — reflect the last LED_STATUS broadcast. Clicking a dot sends LED_CMD to toggle that LED.

### Relay Controller — Relays

- Six tiles showing each relay's live state (green = ON).
- State tracks automatically from RELAY_STATUS (0x101) broadcasts.
- Each tile has a **Toggle** button; there's a global **All OFF** button.

### Viper Interface — Alarm

**Lock / Arm**, **Unlock / Disarm**, and **Remote Start** buttons. The last raw VIPER_STATUS response (0x511) is shown below.

### Bridge Node — Aggregated Panel

The bridge web UI shows one section per discovered node, built from NODE_CAP (0x0F2) frames received over CAN. Includes:
- **Refresh** — re-renders current known node caps
- **Request All Caps** — sends NODE_CAP_REQ (0x0F3) broadcast to prompt nodes that haven't announced recently

Each section shows the controls appropriate for that node's capabilities (relay tiles, switch state, LED dots, viper buttons).

> The **Rules** tab is hidden on the bridge node — rules live on each individual node locally.

---

## 3. Rules Tab

Lists all rules stored in NVS on the current node. Each rule shows a human-readable description of its trigger and action.

**Actions available:**
- **Add** — inline editor: pick trigger ID, byte conditions, action kind, args
- **Edit** — modify an existing rule in place
- **Delete** — remove a single rule
- **Reset to Defaults** — clears all rules and restores `RULES_DEFAULT_INIT` from the compiled firmware

Changes persist to NVS immediately. See [can-bus-reference.md](can-bus-reference.md#rules-engine) for the full rules reference including trigger/action macros and the REST API used by this tab.

---

## 4. Network Tab

Live node presence map showing all nodes that have been seen on the bus:

- Last-seen time for each node
- Transport health indicators (CAN / WiFi)
- Which transport each frame came in on (`can` / `wifi` / `self`)

Nodes that haven't sent a NODE_ANNOUNCE (0x0F0) in the last 15 seconds appear as inactive.

---

## 5. Serial Tab

Live tee of `wlog()`/`wlogln()` output from the connected node — the same text that appears on the UART serial monitor. Useful for watching boot logs, rule firing, and sensor readings without a USB cable.

Also accessible from the hamburger menu on any tab.

---

## 6. Settings (Hamburger Menu)

- **Node ID reassignment** — enter a new ID (0x01–0xFE), sends CONFIG_WRITE (0x400 key 0x01), saves to NVS, and restarts the node.
- **Serial** — opens the Serial tab.

---

## 7. Force WiFi-Only Mode

The checkbox in the page header cuts wired CAN TX on the current node. `bus_tx()` stops using TWAI; all traffic goes over ESP-NOW. Use this to verify the wireless fallback path without disconnecting any hardware.

This setting is **RAM-only** — it resets on power cycle.
