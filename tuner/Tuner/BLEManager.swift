import CoreBluetooth
import Combine

// ── CAN frame model ───────────────────────────────────────────────────────

struct CANFrame: Identifiable {
    let id        = UUID()
    let canID:    UInt16
    let dlc:      UInt8
    let data:     [UInt8]    // always 8 bytes, zero-padded beyond dlc
    let timestamp: Date
    let isOutbound: Bool

    var idHex:   String { String(format: "0x%03X", canID) }
    var dataHex: String {
        data.prefix(Int(dlc)).map { String(format: "%02x", $0) }.joined(separator: " ")
    }
}

// ── BLE manager ───────────────────────────────────────────────────────────

final class BLEManager: NSObject, ObservableObject,
                        CBCentralManagerDelegate, CBPeripheralDelegate {

    // Must match mod_bluetooth.h on the ESP32 side.
    static let serviceUUID = CBUUID(string: "ACC00001-0000-4000-8000-000000000000")
    static let txCharUUID  = CBUUID(string: "ACC00002-0000-4000-8000-000000000000")
    static let rxCharUUID  = CBUUID(string: "ACC00003-0000-4000-8000-000000000000")

    // ── Published state ───────────────────────────────────────────────────

    enum ConnectionState: Equatable {
        case idle, scanning, connecting, discovering, ready, error(String)

        var label: String {
            switch self {
            case .idle:          return "Idle"
            case .scanning:      return "Scanning…"
            case .connecting:    return "Connecting…"
            case .discovering:   return "Setting up…"
            case .ready:         return "Connected"
            case .error(let m):  return "Error: \(m)"
            }
        }
        var isActive: Bool {
            switch self {
            case .scanning, .connecting, .discovering: return true
            default: return false
            }
        }
    }

    @Published var connectionState: ConnectionState = .idle
    @Published var peripheralName: String = ""
    @Published var frames: [CANFrame] = []

    var isReady: Bool { connectionState == .ready }

    // ── Private ───────────────────────────────────────────────────────────

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var rxChar: CBCharacteristic?
    private let maxFrames = 300

    /// UserDefaults key for the last successfully paired peripheral UUID.
    /// On next launch (or BT power-on) the manager will reconnect without scanning.
    private let savedUUIDKey = "lastBLEPeripheralUUID"
    private let restoreKey   = "com.accessorybus.tuner.ble"

    override init() {
        super.init()
#if os(iOS)
        // State restoration lets iOS restart/resume the app in the background
        // when the peripheral comes back in range.
        central = CBCentralManager(delegate: self, queue: .main,
                                   options: [CBCentralManagerOptionRestoreIdentifierKey: restoreKey])
#else
        central = CBCentralManager(delegate: self, queue: .main)
#endif
    }

    // ── Public API ────────────────────────────────────────────────────────

    func startScan() {
        guard central.state == .poweredOn else {
            connectionState = .error("Bluetooth unavailable")
            return
        }
        frames = []
        connectionState = .scanning
        central.scanForPeripherals(withServices: [BLEManager.serviceUUID])
    }

    func stopScan() {
        central.stopScan()
        if connectionState == .scanning { connectionState = .idle }
    }

    func disconnect() {
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        central.stopScan()
        peripheral  = nil
        rxChar      = nil
        frames      = []
        connectionState = .idle
    }

    func clearFrames() {
        frames = []
    }

    /// Inject a CAN frame from the phone onto the ESP32 bus.
    /// id: 11-bit CAN ID; data: up to 8 payload bytes.
    func sendFrame(id: UInt16, data: [UInt8]) {
        guard let rxChar, let peripheral, isReady else { return }
        let dlc = UInt8(min(data.count, 8))
        var buf = [UInt8](repeating: 0, count: 11)
        buf[0] = UInt8(id & 0xFF)
        buf[1] = UInt8(id >> 8)
        buf[2] = dlc
        for i in 0..<Int(dlc) { buf[3 + i] = data[i] }
        peripheral.writeValue(Data(buf), for: rxChar, type: .withoutResponse)
        appendFrame(CANFrame(canID: id, dlc: dlc, data: Array(buf[3...10]),
                             timestamp: Date(), isOutbound: true))
    }

    // ── CBCentralManagerDelegate ──────────────────────────────────────────

#if os(iOS)
    func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        // iOS calls this when it relaunches the app in the background to restore
        // an ongoing BLE session. Grab the peripheral so delegates fire correctly.
        if let peripherals = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],
           let p = peripherals.first {
            peripheral       = p
            peripheral?.delegate = self
            peripheralName   = p.name ?? "CAN Bus Node"
            connectionState  = .connecting
        }
    }
#endif

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            // Try to reconnect to the last known peripheral first (no scan needed).
            if let uuidStr = UserDefaults.standard.string(forKey: savedUUIDKey),
               let uuid = UUID(uuidString: uuidStr),
               peripheral == nil {
                let known = central.retrievePeripherals(withIdentifiers: [uuid])
                if let p = known.first {
                    peripheral      = p
                    peripheralName  = p.name ?? "CAN Bus Node"
                    connectionState = .connecting
                    central.connect(p, options: nil)
                    return
                }
            }
            // Fall through to active scan if no saved peripheral found.
            if connectionState == .scanning {
                central.scanForPeripherals(withServices: [BLEManager.serviceUUID])
            }
        case .poweredOff:
            connectionState = .error("Bluetooth is off")
        default:
            break
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        self.peripheral  = peripheral
        peripheralName   = peripheral.name ?? "CAN Bus Node"
        central.stopScan()
        connectionState  = .connecting
        central.connect(peripheral, options: nil)
    }

    func centralManager(_ central: CBCentralManager,
                        didConnect peripheral: CBPeripheral) {
        // Persist the UUID so we can skip scanning on next launch / BT power cycle.
        UserDefaults.standard.set(peripheral.identifier.uuidString, forKey: savedUUIDKey)
        connectionState = .discovering
        peripheral.delegate = self
        peripheral.discoverServices([BLEManager.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager,
                        didFailToConnect peripheral: CBPeripheral, error: Error?) {
        self.peripheral  = nil
        connectionState  = .error(error?.localizedDescription ?? "Connection failed")
    }

    func centralManager(_ central: CBCentralManager,
                        didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        rxChar          = nil
        connectionState = .idle
        // Re-request connection so iOS will reconnect automatically whenever the
        // peripheral is back in range — even if the app is backgrounded.
        // (The peripheral reference is kept; iOS manages the pending connect.)
        if UserDefaults.standard.string(forKey: savedUUIDKey) != nil {
            connectionState = .connecting
            central.connect(peripheral, options: nil)
        } else {
            self.peripheral = nil
        }
    }

    // ── CBPeripheralDelegate ──────────────────────────────────────────────

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverServices error: Error?) {
        guard error == nil,
              let svc = peripheral.services?.first(where: { $0.uuid == BLEManager.serviceUUID })
        else {
            connectionState = .error("Service not found"); return
        }
        peripheral.discoverCharacteristics([BLEManager.txCharUUID, BLEManager.rxCharUUID], for: svc)
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard error == nil else {
            connectionState = .error("Characteristic discovery failed"); return
        }
        for char in service.characteristics ?? [] {
            if char.uuid == BLEManager.txCharUUID {
                peripheral.setNotifyValue(true, for: char)
            }
            if char.uuid == BLEManager.rxCharUUID {
                rxChar = char
            }
        }
        if rxChar != nil { connectionState = .ready }
    }

    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard error == nil,
              let raw = characteristic.value,
              raw.count == 11 else { return }
        let bytes = [UInt8](raw)
        let canID = UInt16(bytes[0]) | (UInt16(bytes[1]) << 8)
        let dlc   = min(bytes[2], 8)
        appendFrame(CANFrame(canID: canID, dlc: dlc, data: Array(bytes[3...10]),
                             timestamp: Date(), isOutbound: false))
    }

    // ── Private helpers ───────────────────────────────────────────────────

    private func appendFrame(_ frame: CANFrame) {
        frames.append(frame)
        if frames.count > maxFrames {
            frames.removeFirst(frames.count - maxFrames)
        }
    }
}
