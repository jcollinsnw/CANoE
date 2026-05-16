# CANoE Tuner

Native iOS app for the CANoE CAN bus accessory system. Connects to any ESP32 node via **Bluetooth LE** for a live CAN frame log and native GPS injection, or via **Wi-Fi** to open the node's built-in web console.

---

## Requirements

- iOS 17+
- Xcode 15+
- A CANoE node with `ENABLE_BLUETOOTH` defined in its config header

---

## Build

Open `Tuner.xcodeproj` in Xcode, select your device, and run. No third-party dependencies.

**Bundle ID:** `com.accessorybus.tuner`

In Xcode's **Signing & Capabilities** tab, add the **Background Modes** capability and enable:
- Uses Bluetooth LE accessories
- Location updates

These correspond to the `UIBackgroundModes` keys already present in `Info.plist`.

---

## Connection modes

### Bluetooth LE (primary)

Scans for nodes advertising the CANoE service UUID (`ACC00001-0000-4000-8000-000000000000`). Connects to the nearest node, shows a live CAN frame log, and auto-starts phone GPS injection.

**Auto-pair after first connect:** the peripheral UUID is saved to UserDefaults. On every subsequent app launch — or when the node comes back in range after an ignition cycle — iOS reconnects in the background without any user interaction.

### Wi-Fi / Web Console

Connects to the node's SoftAP (`Crustang`) and opens the built-in web console in an embedded browser. The full web UI (CAN frame log, relay controls, rules editor, etc.) runs inside the app. Phone GPS is bridged into the web UI via JavaScript when the GPS toggle is active.

---

## Source files

| File | Description |
|------|-------------|
| `TunerApp.swift` | `@main` entry point; injects `AppSettings` as `@EnvironmentObject` |
| `Theme.swift` | `Color.acapulcoBlue` (#006DAC), `AppSettings` font-scale system, `AppearanceSheet` |
| `ContentView.swift` | Root view — `ConnectView`, `NativeDashboardView`, `NodeBrowserView`, `FrameLogView` |
| `LogView.swift` | Logger tab — live gauge grid, line graph panels, Braun analog dials, session stats |
| `DataLogger.swift` | Session recording, CSV export, `ChannelDescriptor` registry, `CHAN_CAP` decoder |
| `BLEManager.swift` | CoreBluetooth central manager; auto-reconnect, frame log, `sendFrame()` |
| `LocationManager.swift` | CoreLocation wrapper; publishes `GPS_DATA (0x305)` payloads |
| `NodeWebView.swift` | WKWebView with pull-to-refresh and JavaScript GPS bridge |
| `SimulatorDriver.swift` | Synthetic engine data for testing without hardware |
| `WeatherService.swift` | Ambient weather fetch → `ENV_DATA (0x301)` |
| `AltimeterManager.swift` | Barometric pressure from `CMAltimeter` |

---

## Appearance

All accent colors use **Acapulco Blue (#006DAC)**, set as the app's `AccentColor` asset so it applies automatically to native controls (tab bar, pickers, toggles). `Color.acapulcoBlue` is available as a Swift extension for explicit tinting.

Font sizes are configured per category in the **Appearance sheet** (toolbar `textformat.size` icon):

| Category | Used for | Base size |
|----------|----------|-----------|
| Gauge Values | Dial and tile readouts | 20 pt |
| Panel Labels | Graph panel headers, channel names | 14 pt |
| Data Stream | CAN frame log, live readouts | 13 pt |
| Chart Axis | Tick labels on graphs and Y-axes | 9 pt |
| Captions | Units, secondary labels | 10 pt |

Each category has an independent 0.7×–1.8× slider with a live preview. Settings persist in `UserDefaults` via `@AppStorage`. `AppSettings` is an `ObservableObject`; views access it via `@EnvironmentObject`.

**Adding a new source file:** register it in `project.pbxproj` in four places — `PBXBuildFile`, `PBXFileReference`, the Tuner group children list, and the `Sources` build phase.

---

## GATT profile

Matches `mod_bluetooth` on the ESP32. All UUIDs must match exactly.

| UUID | Role |
|------|------|
| `ACC00001-0000-4000-8000-000000000000` | Service |
| `ACC00002-0000-4000-8000-000000000000` | TX — notify — ESP32 → phone (one CAN frame per notification) |
| `ACC00003-0000-4000-8000-000000000000` | RX — write — phone → ESP32 (injects a frame onto the bus) |

**Wire format:** 11 bytes — `[id_lo, id_hi, dlc, d0..d7]`, zero-padded beyond DLC.

---

## GPS injection

When GPS is active, `LocationManager` reads phone speed and heading via CoreLocation and encodes them as a `GPS_DATA (0x305)` frame — the same format the hardware u-blox GPS module uses. The frame is sent to the ESP32 on every location update.

- **BLE mode:** written to the RX characteristic → `bus_tx()` on the node.
- **Wi-Fi mode:** evaluated as `sendFrame(0x305, [...])` in the embedded WKWebView.
- `allowsBackgroundLocationUpdates = true` — GPS keeps running with the screen off.

---

## Permissions (Info.plist)

| Key | Purpose |
|-----|---------|
| `NSBluetoothAlwaysUsageDescription` | Connect to CAN bus nodes via BLE |
| `NSLocationWhenInUseUsageDescription` | Publish phone GPS to the CAN bus |
| `NSLocalNetworkUsageDescription` | Connect to ESP32 nodes on local Wi-Fi |
| `NSAllowsArbitraryLoads` | Allow HTTP connections to ESP32 (no TLS on-device) |

---

## See also

- [mod_bluetooth](../docs/modules/bluetooth.md) — ESP32 firmware module
- [CAN Bus Reference](../docs/can-bus-reference.md) — frame IDs and protocol
