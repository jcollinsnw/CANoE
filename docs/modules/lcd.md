# Module: mod_lcd

HD44780 16×2 LCD driver via PCF8574 I2C backpack. Provides a widget system for modules to claim screen regions, CGRAM slot allocation for custom icons, and a CAN command interface for writing text remotely.

## Enable

```cpp
#define ENABLE_LCD
```

---

## Config defines

| Define | Example | Description |
|--------|---------|-------------|
| `LCD_I2C_ADDR` | `0x27` | PCF8574 I2C address. Try `0x3F` if display stays blank. |
| `LCD_SDA_PIN` | `21` | I2C data pin |
| `LCD_SCL_PIN` | `22` | I2C clock pin |
| `LCD_COLS` | `16` | *(optional)* Display width. Defaults to 16. |
| `LCD_ROWS` | `2` | *(optional)* Display height. Defaults to 2. |

`Wire.begin()` must be called once in `accessory_node.ino` before `lcd_setup()`. Never call it inside a module.

---

## CAN frames

### Receives

| ID | Name | Payload | Behaviour |
|----|------|---------|-----------|
| `0x500` | `LCD_CMD` | `[row, col, char, char, ...]` | Writes text at the given position. Up to 6 chars per frame (8-byte CAN payload minus 2 header bytes). |
| `0x500` | `LCD_CMD` (clear) | `[0xFF]` | Clears the entire display. |

### Sends

None. The LCD only receives commands and manages its own display state.

---

## Idle display layout

```
C✦N✦A✦    ✦-----   ← row 0: transport status + relay bitmap
Last event text      ← row 1: most recent event
```

**Row 0 indicators (cols 0–5):**

| Chars | Meaning |
|-------|---------|
| `C✦` / `C-` | CAN bus healthy / down |
| `N✦` / `N-` | ESP-NOW peer seen / not seen |
| `A✦` / `A-` | SoftAP active / inactive |

Cols 10–15: relay bitmap (one char per relay, `RELAY_n_ICON_ON/OFF`).

**Row 1**: `lcd_set_event(msg)` writes here. Automatically truncated to fit before the first active widget boundary.

---

## Widget system

Modules register screen regions and provide a render callback:

```cpp
LcdWidget w = {
  .row       = 1,
  .col       = 0,
  .width     = 8,
  .refresh_ms = 500,
  .render    = my_render_fn,   // void fn(LcdWidget*)
};
lcd_register_widget(w);
```

`lcd_tick()` (called from `loop()`) re-renders each widget when its `refresh_ms` interval has elapsed. Up to `LCD_MAX_WIDGETS` (8) widgets total.

---

## CGRAM icons

HD44780 has 8 CGRAM slots (0–7):
- Slot 0 is **reserved** — maps to C null terminator (`\x00`), silently truncates `snprintf` output.
- Slots 6–7 are used by the shared CAN/WiFi filled/hollow status icons loaded at end of `lcd_setup()`.
- Slots 1–5 are available for relay icons (5 max).

Allocate a slot:
```cpp
uint8_t slot = lcd_alloc_cgram(bitmap_8bytes);
// slot == 0xFF if no slots remain
```

`relay_icons_init()` allocates relay CGRAM icons. Call it after `lcd_setup()`.

---

## LCD CMD via CAN

Write text from any node on the bus:

```
500 00 00 48 65 6C 6C 6F    write "Hello" at row 0, col 0
500 01 05 41 42 43           write "ABC" at row 1, col 5
500 FF                       clear display
```

Row 0 is the status row — it will be overwritten by the next `lcd_update_status()` tick. Row 1 is the event row and persists until the next `lcd_set_event()` call.

---

## Integration notes

- `lcd_setup()` runs the full HD44780 4-bit init sequence. If the display goes blank (power glitch from relay switching), `lcd_tick()` re-runs init automatically every 30 s — the display self-recovers. A 100 µF decoupling cap across the LCD module VCC/GND pins prevents the glitch entirely.
- `lcd_handle_frame()` must be called for incoming `LCD_CMD` frames.
- `lcd_tick()` must be called every `loop()`.
