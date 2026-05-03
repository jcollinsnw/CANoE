# Module: mod_buzzer

Non-blocking passive piezo tone sequencer. Plays distinct sounds for relay changes, menu interactions, and bus status events. Requires a passive (not active) piezo — the firmware generates frequencies via Arduino `tone()`.

## Enable

```cpp
#define ENABLE_BUZZER
```

---

## Config defines

| Define | Example | Description |
|--------|---------|-------------|
| `BUZZER_PIN` | `16` | GPIO for piezo positive leg. Must not be an input-only pin (avoid 34/35/36/39). An optional 100 Ω series resistor reduces volume. |

---

## CAN frames

### Receives

| ID | Name | Behaviour |
|----|------|-----------|
| `0x100` | `RELAY_CMD` | Plays relay ON or relay OFF chirp based on state change. Fires on self-echoed frames too — relay sounds trigger regardless of whether the command came from a physical switch, the web UI, or the LCD menu. |

`buzzer_handle_frame()` must be called for incoming frames.

---

## Sound events

| Trigger | Sound |
|---------|-------|
| Relay ON | Rising chirp G5 → C6 |
| Relay OFF | Falling chirp C6 → E5 |
| All relays OFF | Descending three-note sweep |
| Menu enter | Rising two-tone E5 → C6 |
| Menu exit | Falling two-tone C6 → E5 |
| Menu scroll | Short tick G5 (18 ms) |
| Enter submenu | Rising two-tone G5 → C6 |
| Execute action | Three-note rising flourish |
| Startup | Boot jingle |
| CAN bus up | Rising confirmation tone |
| CAN bus down | Falling alert tone |
| WiFi client connected | Ascending two-note A5→C6 chime (queued, does not interrupt active sequence) |
| WiFi client disconnected | Descending two-note C6→A5 chime (queued, does not interrupt active sequence) |

Menu sounds are triggered by calls from `mod_menu` (`buzzer_menu_enter()`, `buzzer_menu_scroll()`, etc.) rather than CAN frames. WiFi client sounds fire from the `webui_set_ap_client_cb()` callback in `accessory_node.ino` via `buzzer_wifi_connect()` / `buzzer_wifi_disconnect()`.

---

## Integration notes

- `buzzer_tick()` must be called every `loop()` to advance the sequence non-blocking.
- `ENABLE_BUZZER` uses Arduino `tone()`/`noTone()` on `BUZZER_PIN`. No LEDC channel is consumed.
- `ENABLE_ECU` uses LEDC channel 0 for the carb solenoid PWM. `ENABLE_BUZZER` does **not** conflict with this because `tone()` uses the ESP32's hardware timer independently of LEDC.
- A new `buzzer_play()` / `buzzer_menu_*()` call immediately interrupts any active sequence.
- `buzzer_set_muted(true)` silences all sounds without removing the module.
