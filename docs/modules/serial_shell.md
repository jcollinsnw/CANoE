# Module: mod_serial_shell

Interactive CAN shell over USB serial. Prints received frames and accepts hex CAN commands from the Arduino serial monitor (or any terminal at 115200 baud). Always compiled in — no feature flag.

Output goes to UART only. It does **not** appear in the web UI Serial tab — use `wlog()`/`wlogln()` for that.

## Enable

Always active. No feature flag, no config defines.

---

## Commands

| Command | Description |
|---------|-------------|
| `<id> [b0] [b1] ...` | Send a CAN frame. ID and bytes are hex, space-separated. Up to 8 data bytes. |
| `mon` | Toggle live frame monitor. When on, every frame passing through `serial_shell_print()` is printed to serial. Default: off. |
| `help` | Print command reference. |

Frame format is identical to the web UI hex command line:

```
100 01 01        relay 1 on
100 3F 00        all relays off
101              query relay status (0-byte frame)
510 01           viper arm/lock
510 02           viper unlock
510 03           viper remote start
0F3 FF           request NODE_CAP from all nodes
```

---

## API

```cpp
void serial_shell_setup();                      // call once in setup()
void serial_shell_tick();                       // call every loop()
void serial_shell_print(const BusFrame& f);     // call from frame dispatch loop
```

`serial_shell_print()` is a no-op unless the frame monitor is enabled with `mon`.

---

## Integration notes

- `serial_shell_setup()` must be called after `Serial.begin(115200)` in `setup()`.
- `serial_shell_tick()` reads incoming bytes and dispatches complete lines. Must be called every `loop()` to avoid input buffer overflow.
- `serial_shell_print()` should be called for every frame returned by `bus_rx()` — the monitor feature is useless otherwise.
- Input is line-buffered (up to 40 characters). Lines longer than 40 characters are discarded.
- Output goes directly to `Serial` and is not tee'd to the web UI. To see both serial and web output, use `wlog()`/`wlogln()` in application code.
