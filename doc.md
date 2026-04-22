Created 5 todos

Starting: *Add CAN IDs + EncoderEvent to can_protocol.h (both copies)* (1/5)

Replacing 44 lines with 76 lines in [](file:///Users/jacobcollins/Projects/canbus/firmware/switch_panel/can_protocol.h), [](file:///Users/jacobcollins/Projects/canbus/firmware/relay_controller/can_protocol.h)


```

```

```

```


Starting: *Add Wire.h, LCD/encoder config to switch_panel.ino* (2/5)

Replacing 5 lines with 19 lines in [](file:///Users/jacobcollins/Projects/canbus/firmware/switch_panel/switch_panel.ino)


```

```


Starting: *Add LCD driver section to switch_panel.ino* (3/5)

Replacing 6 lines with 55 lines in [](file:///Users/jacobcollins/Projects/canbus/firmware/switch_panel/switch_panel.ino)


```

```


Starting: *Add handle_lcd_cmd + encoder poll to switch_panel.ino* (4/5)

Replacing 8 lines with 81 lines in [](file:///Users/jacobcollins/Projects/canbus/firmware/switch_panel/switch_panel.ino)


```

```


Starting: *Wire up setup() and loop() calls* (5/5)

Replacing 17 lines with 30 lines in [](file:///Users/jacobcollins/Projects/canbus/firmware/switch_panel/switch_panel.ino)


```

```


Completed: *Wire up setup() and loop() calls* (5/5)

Done. Here's a summary of everything added:

---

### can_protocol.h (both copies synced)
- `CAN_ID_ENCODER_EVENT 0x201` — switch_panel → bus, rotary encoder actions
- `CAN_ID_LCD_CMD 0x500` — anyone → switch_panel, write LCD text
- `EncoderEvent` enum: `ENC_ROTATE_CW/CCW`, `ENC_PRESS`, `ENC_RELEASE`, `ENC_LONG_PRESS`

### switch_panel.ino
- **BTN4 moved** from GPIO 21 → **GPIO 23** (freeing SDA)
- **LCD config**: I2C address `0x27` (try `0x3F` if blank), SDA=21, SCL=22, 16×2
- **LCD driver**: minimal PCF8574 + HD44780 driver using `Wire.h` (no external library). `lcd_init()` called in `setup()`.
- **`handle_lcd_cmd(f)`**: receives `CAN_ID_LCD_CMD` frames — `[row, col, chars...]` writes text, `[0xFF, ...]` clears
- **Encoder config**: CLK=34, DT=35, SW=36 (all input-only GPIOs — **need external 10 kΩ pull-ups to 3V3**)
- **`poll_encoder()`**: gray-code state machine (±4 steps/detent), same debounce logic as switches for the push button. Called every `loop()`.

### Web console usage (LCD)
```
500 01 00 48 65 6C 6C 6F    # row 1, col 0, "Hello"
500 FF                      # clear display
```

Made changes.

Done. Here's what was added:

can_protocol.h — 3 new definitions:

CAN_ID_VIPER_CMD 0x510 — any node sends this to command the alarm
CAN_ID_VIPER_STATUS 0x511 — viper_interface broadcasts raw 5-byte alarm responses
VIPER_CMD_LOCK/UNLOCK/REMOTE_START command byte constants
CFG_TARGET_VIPER 0x03 node target
viper_interface/viper_interface.ino — new sketch, NODE_ID=0x03:

Joins the bus exactly like relay_controller and switch_panel (same bus.h/webui.h setup order)
Receives VIPER_CMD frames → dispatches to ViperESP2.lock(), .unlock(), .remoteStart()
ViperESP2.update() in the loop — when the alarm sends back a 5-byte status packet, it's immediately broadcast as VIPER_STATUS on the bus
Full web console at http://192.168.4.1 on the AccessoryBus AP
index_html.h — added :viper lock|unlock|start alias and three buttons (VIPER LOCK, VIPER UNLOCK, VIPER START). All shared files synced to all three node folders.

One pin note: ViperESP2.cpp hardcodes GPIO 16/17 for UART2. Those are free on the viper_interface ESP32 (they'd only conflict on the relay_controller board, which is a separate device).

