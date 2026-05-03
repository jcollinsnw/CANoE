# Module: mod_rules

CAN-frame-triggered rules engine. Every incoming frame (including self-echoed outbound frames) is evaluated against a table of rules stored in NVS. When a rule's trigger and byte conditions match, its action fires immediately.

Replaces the old hard-coded switch action map — all switch→relay/LED/viper/menu mappings live here and are editable at runtime without reflashing.

## Enable

```cpp
#define ENABLE_RULES
```

---

## Config defines

| Define | Example | Description |
|--------|---------|-------------|
| `MAX_RULES` | `16` | Number of NVS slots reserved for rules. More = more flash. |
| `RULES_DEFAULT_INIT` | *(see below)* | *(Optional)* Compile-time defaults written to NVS on first boot and after factory reset. Omit to start with an empty table. |

### Example RULES_DEFAULT_INIT

```cpp
#define RULES_DEFAULT_INIT {                           \
  RULE(TRIG_SW_PRESS(0),   ACT_RELAY_TOGGLE(0)),       \
  RULE(TRIG_SW_PRESS(1),   ACT_RELAY_TOGGLE(1)),       \
  RULE(TRIG_SW_PRESS(4),   ACT_RELAY_ON(4)),           \
  RULE(TRIG_SW_RELEASE(4), ACT_RELAY_OFF(4)),          \
  RULE(TRIG_SW_PRESS(7),   ACT_ALL_OFF()),             \
}
```

---

## CAN frames

The rules engine does not own any CAN IDs. It evaluates all frames and dispatches actions via `bus_tx()`. The action modules (relay, LED, viper, menu) own the resulting outbound frames.

### Trigger reference

| Macro | Trigger ID | Condition 0 | Condition 1 |
|-------|-----------|-------------|-------------|
| `TRIG_SW_PRESS(idx)` | 0x200 | data[0] == idx | data[1] == 0x01 |
| `TRIG_SW_RELEASE(idx)` | 0x200 | data[0] == idx | data[1] == 0x00 |
| `TRIG_SW_LONG(idx)` | 0x200 | data[0] == idx | data[1] == 0x02 |
| `TRIG_RELAY_BIT_ON(n)` | 0x101 | bit n set in data[0] | — |
| `TRIG_RELAY_BIT_OFF(n)` | 0x101 | bit n clear in data[0] | — |
| `TRIG_RELAY_CMD_ON(n)` | 0x100 | mask includes bit n | state bit n == 1 |
| `TRIG_RELAY_CMD_OFF(n)` | 0x100 | mask includes bit n | state bit n == 0 |
| `TRIG_BOOT()` | 0x0F1 | — | — |
| `TRIG_BUS_ERROR()` | 0x0F4 | — | — |
| `TRIG_CAN_OK()` | 0x0F4 | error_code == 0 | — |
| `TRIG_ANY(id)` | id | — | — |

Custom triggers: set `trig_id` directly and write `c0_byte / c0_val / c0_mask` by hand. A condition is skipped when its mask is `0x00`.

Match logic: `(frame.data[c_byte] & c_mask) == (c_val & c_mask)`

### Action reference

| Macro | Action | Args |
|-------|--------|------|
| `ACT_RELAY_TOGGLE(r)` | Toggle relay | r = relay index 0–5 |
| `ACT_RELAY_ON(r)` | Relay on | r = relay index 0–5 |
| `ACT_RELAY_OFF(r)` | Relay off | r = relay index 0–5 |
| `ACT_ALL_OFF()` | All relays off | — |
| `ACT_RELAY_SCENE(bmap)` | Set relay bitmap | bmap = 6-bit bitmap |
| `ACT_LED_ON(node, led)` | LED on | node = target node ID; led = index 0-based |
| `ACT_LED_OFF(node, led)` | LED off | same |
| `ACT_WIFI_ENABLE(node)` | Enable WiFi on node | node = target node ID |
| `ACT_WIFI_DISABLE(node)` | Disable WiFi on node | same |
| `ACT_VIPER(cmd)` | Viper command | cmd: 0x01 lock, 0x02 unlock, 0x03 start |
| `ACT_MENU_SELECT()` | Menu navigate / confirm | — |
| `ACT_MENU_ENTER()` | Menu enter / execute | — |
| `ACT_BUZZER_ALERT()` | 3 urgent pulses on local node | — |
| `ACT_BUZZER_PLAY(node, seq)` | Send `BUZZER_CMD` to target node | node = target node ID or 0xFF; seq = `BUZZER_SEQ_*` constant |
| `ACT_BUZZER_PLAY_ARG(node, seq, arg)` | Same with extra arg | arg = e.g. peer count for `BUZZER_SEQ_PEER` |
| `ACT_RELAY_TIMED_OFF(r, secs)` | Relay on then off after timeout | r = relay index 0–5; secs = delay in seconds (1–255) |
| `ACT_LED_FLASH(node, led, period_ds)` | Flash an LED | node = target; led = LED index; period_ds = period in 100 ms units |

---

## Runtime management

Rules are managed via the **Rules tab** in the web console or the REST API (used internally by the tab):

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/rules` | Return all rules as JSON |
| POST | `/api/rules` | Create or update a rule |
| DELETE | `/api/rules?idx=N` | Delete rule at index N |
| POST | `/api/rules/reset` | Factory reset to `RULES_DEFAULT_INIT` |

Changes persist to NVS immediately. The factory reset button in the Rules tab restores the compiled defaults. Each write/delete/reset also emits `BLOB_WRITE` + `BLOB_COMMIT` frames (`BLOB_NS_RULES 0x02`) so rule changes are visible in the CAN frame log.

---

## HOLD-style rules

Relay on while switch held, off on release — expressed as two rules:

```cpp
RULE(TRIG_SW_PRESS(4),   ACT_RELAY_ON(4)),    // press → relay 5 on
RULE(TRIG_SW_RELEASE(4), ACT_RELAY_OFF(4)),   // release → relay 5 off
```

The relay watchdog (`RELAY_MAX_ON_INIT`) provides a backstop if the release event is missed.

---

## Integration notes

- `rules_handle_frame()` must be called for every frame returned by `bus_rx()`.
- Rules evaluate all frames including self-echoed ones (`source == "self"`). A rule triggered by `RELAY_CMD` (0x100) can therefore react to web-UI-injected frames exactly like physical switch presses.
- `TRIG_BOOT()` fires on the self-echoed `BOOT_EVENT (0x0F1)` at the end of `setup()`. Use it to set initial relay/LED states on power-up.
- NVS namespace is `"rules"`, keys `"r0"` through `"r<MAX_RULES-1>"`. Each rule is a 12-byte blob.
