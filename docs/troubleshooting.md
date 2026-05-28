# CANoE — Troubleshooting

---

## CAN Bus

**Bus not working / no frames**
- Confirm `USE_CAN_TRANSCEIVER` is the **same value** on all nodes. A mismatch (one node push-pull, another open-drain) causes bus errors on every frame.
- Bench mode: confirm the shared pull-up resistor is present on the wire (1–4.7 kΩ to 3V3).
- Production mode: confirm 120 Ω termination at each physical end; check transceiver VCC and GND.
- Serial monitor shows `[CAN] init failed` if `twai_driver_install` returns an error — usually a GPIO conflict.
- The web UI header shows `CAN: OK` / `CAN: LOST` based on RX activity in the last 5 seconds.

---

## Relay Controller

**Relay doesn't fire**
- Check `RELAY_ACTIVE_HIGH = true` — correct for ULN2803 (low-side switching). Flip if using a high-side driver.
- Confirm ULN2803 COM pin is connected to 12 V. Without it the internal flyback diodes have no rail and the output transistors may not saturate.
- Confirm GPIO 16–22 on the relay_controller board aren't used for something else.

**Relay cuts off unexpectedly**
- A per-relay `max_on_ms` safety timer is in effect. Default for relay 5 (horn) is 30,000 ms. Check with `:readcfg relay` and adjust with `:cfgrelay <idx> maxon <ms>`.
- **Fuel pump (R1) cuts off** is more likely the [fuel pump safety FSM](modules/fuel_pump.md), not the relay watchdog. Symptoms: pump goes off, switch panel buzzer plays the `BUZZER_SEQ_FUEL_PUMP_OFF` alarm (4 alternating low/high pulses), Cardputer CLI bar shows "FUEL PUMP CUT". A `FUEL_PUMP_STATE (0x311)` frame with `reason=4 stall` appears in the frame log. See the Fuel Pump Safety section below.

---

## Fuel Pump Safety

**Pump never turns on after boot**
- Check the current mode. Send `401 02 61 00` (CONFIG_READ_REQ) — the relay node should reply with the current mode in `data[4]`. Default is `3` (BOTH).
- In `BOTH` mode, both RPM and COIL gates must pass within `FUEL_PUMP_STALL_MS` of each other. If the coil-sense wiring is broken or the RPM optocoupler isn't conducting, the pump won't leave ARMED.
- Quick diagnosis: switch to `RPM` (mode 1) or `COIL` (mode 2) via `400 02 61 00 00 01 00 00 00` to isolate which gate is failing. Or fully disable with mode 0 if you need to drive the car right now.

**Pump cycles on/off at idle or during cranking**
- `FUEL_PUMP_STALL_MS` (default 2000) may be too tight given the RPM sample rate or coil-sense hysteresis. Lengthen it in `relay_controller.h` and reflash.
- RPM gate: confirm `RPM_SAMPLE_MS < FUEL_PUMP_STALL_MS / 2`. The relay node samples at 500 ms by default → comfortable margin.
- COIL gate: hysteresis (`IGN_COIL_ON_THRESHOLD_CV` / `IGN_COIL_OFF_THRESHOLD_CV`) defaults are 6.00 V / 4.00 V. If the dash ballast drops the voltage further than expected, the on-threshold may never be crossed — measure with a multimeter at the divider input.

**Buzzer keeps alerting every time I turn the key off**
- Normal behavior. The FSM detects loss of all gates as a stall (it can't distinguish "user turned key off" from "engine quit"). The alarm fires once per RUNNING → ARMED transition.
- Mute the switch panel buzzer with `104 01 20 01` if it's bothering you (`BUZZER_CMD_MUTE`). Unmute with `104 01 20 00`.

---

## LCD / Display

**LCD stays blank after boot**
- Try I2C address `0x3F` instead of `0x27` (change `LCD_I2C_ADDR` in `switch_panel.h` or `viper_interface.h`).
- Confirm LCD VCC is on 5 V, not 3.3 V.
- Use an I2C scanner sketch to confirm the PCF8574 backpack is visible on the bus.

**LCD goes blank or shows garbage intermittently**
- Relay coil switching can momentarily dip the LCD supply voltage, causing the HD44780 to lose its init state. Solder a **100 µF electrolytic capacitor** directly across the LCD module's VCC and GND pins.
- The firmware automatically re-runs the full init sequence every 30 seconds — the display self-recovers within that window.
- The brief garbage flash during re-init is normal (display-off / clear / display-on cycle) and harmless.

---

## Encoder

**Encoder behaves erratically / skips detents**
- GPIOs 34/35 are input-only with no internal pull-up, but the CJMCU-111 has onboard pull-ups. Confirm the module VCC pin is connected to 3V3. If VCC is floating the signals will be noisy.
- Erratic behaviour after VCC is confirmed is usually an intermittent wire — check solder joints on the GA/GB pins.

---

## Viper Interface

**Viper commands have no effect**
- Confirm the level shifter is bidirectional and powered (LV = 3V3, HV = 5V, shared GND).
- Run `ViperESP2::sniff()` in the main loop (temporarily) and check serial for raw bytes from the alarm — confirms RX is alive.
- Verify TX and RX are not swapped at the Viper connector.
- Check that `[cmd via can]` or `[cmd via wifi]` appears in serial when you send `:viper lock` — this confirms the CAN frame reached the viper_interface node.

---

## WiFi / ESP-NOW Fallback

**ESP-NOW fallback not working**
- Confirm all nodes use the same WiFi channel (hardcoded to channel 6 in `webui.cpp`).
- Received frames show `[via wifi]` in the frame log when the wired bus is cut and ESP-NOW is carrying traffic.
- Use "force wifi-only" in the web UI header to simulate a wire failure without disconnecting anything.

---

## GPIO / Strapping Pins

**GPIO 13 (SW3 or ECU mode switch) misbehaves at boot**
- GPIO 13 is a strapping pin on some ESP32 modules. A switch pulled LOW during power-on reset can force download mode or cause erratic boot behavior.
- Switch panel: move SW3 to a non-strapping GPIO and update `INPUT_PINS_INIT` in `switch_panel.h`.
- ECU node: use a normally-open switch so GPIO 13 defaults HIGH (injection mode) at boot. Switch to carb mode via CAN after boot: `308 01 00 00 00`.

---

## ECU Node

**Engine reads consistently rich or lean across all RPM**
- `BASE_PW` is wrong. Check the boot log for `[ecu] base_pw=XXXX us` and verify `ECU_DISPLACEMENT_CC`, `ECU_CYLINDERS`, and `ECU_INJ_CC_MIN` match your engine.
- Adjust at runtime without reflashing:
  ```
  400 04 53 00 00 A0 0F 01    # CONFIG_WRITE: ECU (0x04), key 0x53, value 4000 µs, persist
  ```

**Short-term fuel trim (STFT) rails at ±30%**
- Trim hitting its limit means `BASE_PW` needs adjustment — the PI loop can't correct far enough.
- Trim at +30% = lean (increase `BASE_PW`); −30% = rich (decrease it).
- Watch ECU_DATA (0x307) flags byte in the frame log: `bit0=closed_loop`, `bit1=enriching`.

**No RPM reading**
- Confirm the external 10 kΩ pull-up from GPIO 34 to 3.3V is present. Without it the pin floats and may count noise or read nothing.
- Verify the PC817C LED side is conducting: at cranking speeds the optocoupler should feel faintly warm. Cold = LED not conducting (wrong resistor, reversed polarity, or wrong coil tap).
- On HEI distributors, the coil negative terminal may not produce a clean square wave — try the tach output terminal on the distributor cap instead.

**Analog readings all zero or stuck**
- ADC2 conflict. When WiFi is active, ADC2 (GPIOs 0, 2, 4, 12–15, 25–27) is unavailable. All ECU analog inputs must be on ADC1 (GPIOs 32–39).
- Verify the voltage divider: with no sensor connected, the ADC pin should read ~1.65V (midpoint of the divider). If it reads 0V or 3.3V the divider is open or shorted.

**Injectors fire continuously or not at all**
- **Continuous:** Check the gate pull-down resistor (10 kΩ gate to GND). A floating gate can latch the MOSFET on. Verify the part is actually an IRLZ44N — a standard IRFZ44N may partially conduct at 3.3V and dissipate heat even at "off" duty.
- **Not firing:** Confirm +12V fused power reaches the injector positive terminal. Measure drain-to-source voltage — at pulse time it should switch from ~12V to ~0V. No switch = MOSFET not turning on.
- **Flyback diode:** A missing or backwards 1N5822 destroys the MOSFET quickly. Verify cathode (banded end) connects to +12V, anode to MOSFET drain.

**WBO2 reads wrong AFR**
- Check `WBO2_MIN_V` and `WBO2_MAX_V` in `ecu_node.h` are set to the voltage **at the ADC pin after the 2:1 divider**, not the raw controller output. For an Innovate LC-2 outputting 0–5V, these should be `0.0` and `2.5`.
- Verify the wideband controller is warmed up — the LC-2 takes ~60 seconds and outputs a fixed voltage during warm-up.
- At idle near stoich, the ADC pin should read roughly `(14.7 − WBO2_MIN_AFR) / (WBO2_MAX_AFR − WBO2_MIN_AFR) × 2.5V`. Cross-check with a multimeter.
