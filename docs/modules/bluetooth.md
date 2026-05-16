# Module: mod_bluetooth

BLE GATT CAN bus mirror. Exposes every CAN frame seen by the node as a BLE notification, and accepts frames from a phone that are injected onto the bus via `bus_tx()`. Designed as the primary native interface for the [Tuner iOS app](../../tuner/README.md).

Runs concurrently with the SoftAP web console — both transports stay active. A connected BLE client receives every frame regardless of whether it arrived over TWAI or ESP-NOW.

## Enable

```cpp
#define ENABLE_BLUETOOTH
```

Optionally override the advertised BLE device name (defaults to `NODE_NAME`):

```cpp
#define BLE_DEVICE_NAME "Crustang Switch"
```

---

## GATT profile

| UUID | Type | Direction | Description |
|------|------|-----------|-------------|
| `ACC00001-0000-4000-8000-000000000000` | Service | — | CANoE CAN bus service |
| `ACC00002-0000-4000-8000-000000000000` | Characteristic — notify | ESP32 → phone | One CAN frame per notification |
| `ACC00003-0000-4000-8000-000000000000` | Characteristic — write (w/o response) | Phone → ESP32 | Phone injects a CAN frame |

### Wire format

Fixed 11-byte frame:

```
byte  0–1  : CAN ID, little-endian uint16  (11-bit)
byte  2    : DLC (0–8)
bytes 3–10 : payload, zero-padded beyond DLC
```

---

## Thread model

`bluetooth_handle_frame()` and `bluetooth_loop()` run on the Arduino main task only.

The BLE write callback fires on the BT task — incoming phone→ESP32 frames are pushed into an 8-slot spinlock-protected ring buffer and drained on the main task in `bluetooth_loop()`, where they call `bus_tx()` normally.

Outgoing ESP32→phone frames use a 32-slot ring buffer on the main task (no locking). Frames are sent as BLE notifications in `bluetooth_loop()` immediately after the per-module work.

On disconnect the module automatically restarts advertising so the next ignition cycle reconnects without any user action.

---

## iOS Tuner app — auto-pair & background GPS

The companion Tuner iOS app (`tuner/`) connects via CoreBluetooth.

**First run:** tap "Scan for CAN Bus" in the app; it saves the peripheral UUID to UserDefaults.

**Every subsequent ignition cycle:**

1. Node powers on → calls `bluetooth_setup()` → starts advertising.
2. iOS wakes/resumes the app in the background via the `bluetooth-central` background mode + `CBCentralManagerOptionRestoreIdentifierKey`.
3. `BLEManager` calls `retrievePeripherals(withIdentifiers:)` with the saved UUID — **no scan** — and calls `connect()` directly.
4. On connect, `NativeDashboardView` appears and auto-starts CoreLocation (`allowsBackgroundLocationUpdates = true`).
5. Phone speed/heading is encoded as `GPS_DATA (0x305)` frames and written to the RX characteristic every GPS update, even with the screen locked.

When the node loses power (ignition off), `BLEManager` leaves a pending `connect()` with iOS. The reconnect completes the moment the node comes back in range on the next startup.

---

## Config reference

| Define | Example | Description |
|--------|---------|-------------|
| `ENABLE_BLUETOOTH` | — | Compile-in the module |
| `BLE_DEVICE_NAME` | `"Crustang Switch"` | BLE advertised name. Defaults to `NODE_NAME`. |

### Runtime control via CAN

Both commands use `CONFIG_WRITE (0x400)` — see [CAN Bus Reference: Runtime Configuration](../can-bus-reference.md#runtime-configuration) for the full frame layout.

| Key | Constant | Effect |
|-----|----------|--------|
| `0x33` | `CFG_KEY_BT_ENABLED` | `arg=1` enable / `arg=0` disable BLE. Saves `bt_en` to NVS and restarts the node. |
| `0x34` | `CFG_KEY_BT_ADVERTISING` | `arg=1` start / `arg=0` stop advertising. Runtime only — no restart, no NVS write. |

```
# Disable BLE on relay_controller (0x02), persists across reboots
400 02 33 00 00 00 00 00 00

# Stop advertising without rebooting (blocks new connections; existing one stays)
400 02 34 00 00 00 00 00 00

# Resume advertising
400 02 34 00 00 01 00 00 00
```

---

## Typical node config

```cpp
#define ENABLE_BLUETOOTH
#define BLE_DEVICE_NAME  NODE_NAME        // or a custom string
```

---

## CAN frames

### Mirrors (all frames)

Every frame received by `bluetooth_handle_frame()` — sourced from TWAI, ESP-NOW, or self-echo — is forwarded to the connected phone as a BLE notification.

### Injects (phone-originated)

Any valid 11-byte write to the RX characteristic is decoded and passed to `bus_tx()`. It enters the bus exactly as if injected from the web console and is self-echoed back through all module handlers.

---

## Dependencies

- `BLEDevice.h`, `BLEServer.h`, `BLECharacteristic.h`, `BLE2902.h` — all included in the Arduino-ESP32 core.
- No additional libraries required.

---

## Notes

- BLE and WiFi share the same radio on the ESP32. The coexistence scheduler handles time-division; no firmware changes are needed. CAN bus frame rates (≤ a few hundred Hz) are well within what BLE can handle.
- `BLE_TX_RING` (32) and `BLE_RX_RING` (8) can be increased in `mod_bluetooth.cpp` if frame bursts are dropped under high traffic.
- On ESP32, BLE initialization consumes ~80 KB of heap. If the node is already close to the heap limit with other modules enabled, watch for allocation failures at boot.
