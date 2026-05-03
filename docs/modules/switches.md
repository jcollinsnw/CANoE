# Module: mod_switches

Debounced input reader for latching switches, momentary buttons, and a rotary encoder. Input-only — this module publishes events to the bus and has no action dispatch of its own. All switch-to-action logic lives in [mod_rules](rules.md).

## Enable

```cpp
#define ENABLE_SWITCHES
```

---

## Config defines

| Define | Example | Description |
|--------|---------|-------------|
| `NUM_SWITCHES` | `3` | Number of latching toggle/rocker switches. These are the first N entries in `INPUT_PINS_INIT`. |
| `NUM_BUTTONS` | `7` | Number of momentary push-buttons. These follow the switches in `INPUT_PINS_INIT`. |
| `INPUT_PINS_INIT` | `{25,26,27,32,33,13,14,18,36,39}` | GPIO list, switches first then buttons. All use `INPUT_PULLUP` except GPIO 36 / 39 (input-only, no internal pull-up — require external 10 kΩ to 3V3). |
| `ENC_CLK_PIN` | `34` | Rotary encoder GA (CLK/A). CJMCU-111 has onboard pull-ups; connect module VCC to 3V3. |
| `ENC_DT_PIN` | `35` | Rotary encoder GB (DT/B). Same note as above. |

Switch IDs in CAN frames: `0`–`(NUM_SWITCHES-1)` are latching switches, `NUM_SWITCHES`–`(NUM_SWITCHES+NUM_BUTTONS-1)` are buttons.

---

## CAN frames

### Sends

| ID | Name | Payload | Triggered by |
|----|------|---------|--------------|
| `0x200` | `SWITCH_EVENT` | `[input_id, event]` | Any debounced state change on a switch or button |
| `0x201` | `ENCODER_EVENT` | `[event, count]` | Encoder rotation or button press |

#### SWITCH_EVENT events (`data[1]`)

| Value | Constant | Meaning |
|-------|----------|---------|
| `0x00` | `SW_RELEASE` | Switch opened / button released |
| `0x01` | `SW_PRESS` | Switch closed / button pressed |
| `0x02` | `SW_LONG_PRESS` | Button held ≥ 600 ms |
| `0x03` | `SW_DOUBLE_PRESS` | Two presses within the double-press window |

#### ENCODER_EVENT events (`data[0]`)

| Value | Constant | Meaning |
|-------|----------|---------|
| `0x00` | `ENC_ROTATE_CW` | Clockwise rotation; `data[1]` = detent count |
| `0x01` | `ENC_ROTATE_CCW` | Counter-clockwise; `data[1]` = detent count |
| `0x02` | `ENC_PRESS` | Encoder shaft button pressed |
| `0x03` | `ENC_RELEASE` | Encoder shaft button released |
| `0x04` | `ENC_LONG_PRESS` | Encoder shaft held ≥ 600 ms |

### Receives

None. `mod_switches` is input-only.

---

## Integration notes

- Switches and buttons work on any GPIO that supports `INPUT_PULLUP`. GPIOs 34, 35, 36, and 39 are input-only with no internal pull-up — `INPUT_PULLUP` is silently ignored. Wire external 10 kΩ resistors to 3V3 for those pins.
- The CJMCU-111 encoder module has 3.3 kΩ onboard pull-ups on GA and GB. Connect module VCC to 3V3; no external resistors needed for the encoder pins.
- The CJMCU-111 shaft does not wire its SW contact to any pin header. Use a dedicated button (BTN4, GPIO 14) for encoder select/back actions and wire it through the rules engine.
- GPIO 13 is a strapping pin on some ESP32 modules. If a button on GPIO 13 triggers erratic behaviour at boot, move it to a non-strapping GPIO and update `INPUT_PINS_INIT`.
- `switches_loop()` must be called every `loop()` iteration for debouncing to work correctly.
