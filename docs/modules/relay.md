# Module: mod_relay

6-channel relay GPIO driver with per-relay safety watchdog and CAN control. The relay controller is the only node that drives physical relay outputs, but any node can send `RELAY_CMD` frames to control them.

> **Battery voltage telemetry** is handled by `mod_battery` (enabled via `ENABLE_BATTERY`), not mod_relay. Any node with ADC pins wired to voltage dividers can broadcast TELEMETRY (0x300). See `switch_panel.h` or `relay_controller.h` for pin definitions.

## Enable

```cpp
#define ENABLE_RELAY
```

---

## Config defines

| Define | Example | Description |
|--------|---------|-------------|
| `NUM_RELAYS` | `6` | Number of relay outputs (max 6) |
| `RELAY_ACTIVE_HIGH` | `true` | `true` for ULN2803 (low-side, GPIO HIGH → relay ON). `false` for high-side drivers. |
| `RELAY_PINS_INIT` | `{16,17,18,19,21,22}` | GPIO for each relay (index 0–5) |
| `RELAY_MAX_ON_INIT` | `{0,0,0,0,30000,0}` | Per-relay safety auto-off in ms. `0` = no limit. Relay 5 (horn) defaults to 30 s. |

**LCD relay widget** (requires `ENABLE_LCD`):

| Define | Example | Description |
|--------|---------|-------------|
| `RELAY_WIDGET_ROW` | `0` | LCD row for relay status bitmap |
| `RELAY_WIDGET_COL` | `10` | Starting column |
| `RELAY_WIDGET_WIDTH` | `6` | Width in characters (one per relay) |
| `RELAY_DISPLAY_COUNT` | `6` | Number of relays to show in the bitmap |
| `RELAY_n_LABEL` | `"Headlights"` | Human-readable name for relay n (1–6). Default: `"Relay N"` |
| `RELAY_n_ICON_ON` | `{0b11111,...}` | 8-byte HD44780 5×8 bitmap for relay n ON state |
| `RELAY_n_ICON_OFF` | `{0b00000,...}` | 8-byte HD44780 5×8 bitmap for relay n OFF state |

HD44780 CGRAM has 8 slots; slot 0 is reserved and 2 are used by CAN/WiFi status icons, leaving **5 slots** for relay icons total.

---

## CAN frames

### Receives

| ID | Name | Payload | Behaviour |
|----|------|---------|-----------|
| `0x100` | `RELAY_CMD` | `[mask, state]` | Sets relay outputs. Only bits set in `mask` are changed. Bit 0 = relay 1, bit 5 = relay 6. Resets the per-relay watchdog timer. |
| `0x400` | `CONFIG_WRITE` | `[target, key, ...]` | Handles keys `0x20` (per-relay max-on-ms) and `0x01` (node ID reassignment). |
| `0x401` | `CONFIG_READ_REQ` | `[target, key, idx]` | Replies with `CONFIG_READ_RESP (0x402)` for relay timeout values. |
| `0x403` | `CONFIG_SAVE` | `[target, action]` | Commit / reload / factory-reset relay NVS config. |

### Sends

| ID | Name | Payload | Rate |
|----|------|---------|------|
| `0x101` | `RELAY_STATUS` | `[bitmap]` | 5 Hz. Bit 0 = relay 1 state, bit 5 = relay 6. |
| `0x402` | `CONFIG_READ_RESP` | same layout as CONFIG_WRITE | In response to `CONFIG_READ_REQ`. |

---

## Relay bitmap

```
bit 0 = relay 1 (0x01)    bit 3 = relay 4 (0x08)
bit 1 = relay 2 (0x02)    bit 4 = relay 5 (0x10) — horn
bit 2 = relay 3 (0x04)    bit 5 = relay 6 (0x20)
all   = 0x3F
```

Examples:
```
100 01 01    relay 1 ON
100 01 00    relay 1 OFF
100 10 10    relay 5 (horn) ON
100 3F 00    all relays OFF
100 03 01    relay 1 ON, relay 2 OFF, relays 3–6 unchanged
```

---

## Safety watchdog

Each relay has a `max_on_ms` timer. When a relay has been on longer than its limit, the watchdog turns it off automatically. Default for relay 5 (horn) is 30,000 ms; all others default to 0 (unlimited).

Configure at runtime via CAN or web console:
```
:cfgrelay 4 maxon 5000 !    relay 5 (horn) cuts off after 5 s, persist
:cfgrelay 4 maxon 0 !       remove the limit
:readcfg relay              dump all current timeouts
:save relay                 persist current RAM config to NVS
```

---

## Integration notes

- `relay_setup()` must be called **first** in `setup()` — before CAN, WiFi, or any other module — so GPIO outputs are in a known state before any traffic arrives.
- `relay_handle_frame()` must be called for every frame returned by `bus_rx()`.
- `relay_icons_init()` must be called after `lcd_setup()` if `ENABLE_LCD` is defined — it loads relay labels and allocates CGRAM icons.
- `RELAY_ACTIVE_HIGH true` is correct for a ULN2803A (low-side Darlington). With a ULN2803A, GPIO HIGH drives the Darlington base → output pulls coil to GND → relay energises. Flip to `false` if using a high-side driver.
