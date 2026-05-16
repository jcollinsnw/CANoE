import Foundation
import Combine

// MARK: - Channel descriptor

struct ChannelDescriptor: Identifiable, Equatable, Hashable {
    let id:     String              // stable key used in GraphConfig and UserDefaults
    let label:  String
    let unit:   String
    let yRange: ClosedRange<Double>
    let format: String             // printf format for value labels

    func normalized(_ value: Double) -> Double {
        let r = yRange
        return max(0, min(100, (value - r.lowerBound) / (r.upperBound - r.lowerBound) * 100))
    }

    static func == (lhs: Self, rhs: Self) -> Bool { lhs.id == rhs.id }
    func hash(into hasher: inout Hasher) { hasher.combine(id) }
}

// Built-in channel definitions — referenced by the decoder registry below.
extension ChannelDescriptor {
    static let rpm         = ChannelDescriptor(id: "rpm",         label: "RPM",     unit: "rpm", yRange: 0...8000,  format: "%.0f")
    static let tps         = ChannelDescriptor(id: "tps",         label: "TPS",     unit: "%",   yRange: 0...100,   format: "%.0f")
    static let afr         = ChannelDescriptor(id: "afr",         label: "AFR",     unit: ":1",  yRange: 10...18,   format: "%.2f")
    static let map         = ChannelDescriptor(id: "map",         label: "MAP",     unit: "kPa", yRange: 20...110,  format: "%.0f")
    static let coolantC    = ChannelDescriptor(id: "coolantC",    label: "Coolant", unit: "°C",  yRange: 20...120,  format: "%.0f")
    static let intakeC     = ChannelDescriptor(id: "intakeC",     label: "IAT",     unit: "°C",  yRange: -10...60,  format: "%.0f")
    static let batteryV    = ChannelDescriptor(id: "batteryV",    label: "Battery", unit: "V",   yRange: 11...15,   format: "%.2f")
    static let auxBatteryV = ChannelDescriptor(id: "auxBatteryV", label: "Aux Bat", unit: "V",   yRange: 11...15,   format: "%.2f")
    static let speed       = ChannelDescriptor(id: "speed",       label: "Speed",   unit: "mph", yRange: 0...120,   format: "%.1f")
}

// MARK: - Frame extractor

// Extracts one channel value from a CAN frame payload.
struct FrameExtractor {
    let descriptor: ChannelDescriptor
    let minDLC:     UInt8
    let extract:    ([UInt8]) -> Double?   // nil = skip (missing fix, zero sensor, etc.)
}

// MARK: - Graph config (persisted via UserDefaults)

enum GraphType: String, Codable {
    case line, gauge
}

struct GraphConfig: Identifiable, Codable {
    var id:            UUID      = UUID()
    var title:         String    = "Graph"
    var channels:      [String]  = []
    var windowSeconds: Double    = 30
    var graphType:     GraphType = .line

    // Custom init so callers can omit graphType (defaults to .line).
    init(id: UUID = UUID(), title: String = "Graph",
         channels: [String] = [], windowSeconds: Double = 30,
         graphType: GraphType = .line) {
        self.id            = id
        self.title         = title
        self.channels      = channels
        self.windowSeconds = windowSeconds
        self.graphType     = graphType
    }

    // Backward-compatible decoding: old saved configs lack "graphType" → default .line.
    private enum CodingKeys: String, CodingKey {
        case id, title, channels, windowSeconds, graphType
    }
    init(from decoder: Decoder) throws {
        let c      = try decoder.container(keyedBy: CodingKeys.self)
        id            = try c.decode(UUID.self,     forKey: .id)
        title         = try c.decode(String.self,   forKey: .title)
        channels      = try c.decode([String].self, forKey: .channels)
        windowSeconds = try c.decode(Double.self,   forKey: .windowSeconds)
        graphType     = try c.decodeIfPresent(GraphType.self, forKey: .graphType) ?? .line
    }
}

// MARK: - Data point

struct DataPoint: Identifiable {
    let id         = UUID()
    let timestamp:  Date
    let value:      Double
    let normalized: Double  // 0–100 within channel.yRange
}

// MARK: - Logger

final class DataLogger: ObservableObject {

    // Channels that have published at least one value — auto-grows as frames arrive.
    @Published var channels:  [String: ChannelDescriptor] = [:]

    // Live current values, keyed by channel ID.
    // Not @Published — updated on every frame; views refresh via the 10 Hz uiRefreshTimer.
    var current:   [String: Double] = [:]

    // Rolling history per channel (up to historyWindow seconds), keyed by channel ID.
    // Not @Published — same reason as current.
    var history:   [String: [DataPoint]] = [:]

    // Recording state
    @Published var isRecording      = false
    @Published var sessionDuration:  TimeInterval = 0

    // Session stats (populated while recording, retained after stop)
    @Published var sessionMin:  [String: Double] = [:]
    @Published var sessionMax:  [String: Double] = [:]
    @Published var sessionAvg:  [String: Double] = [:]

    // Persisted graph layout
    @Published var graphs: [GraphConfig] = [] {
        didSet { saveGraphs() }
    }

    let historyWindow: TimeInterval = 3600   // 1 hour rolling buffer (~3600 pts/channel at 1 Hz)

    // Decoders built at runtime from CHAN_CAP (0x321) frames. Supplements the static
    // registry; static decoders take priority on any channel they already handle.
    var dynamicDecoders: [UInt16: [FrameExtractor]] = [:]

    // All channels in the decoder registry (whether or not they have published yet)
    // plus any dynamically discovered channels. Used to populate the graph editor.
    var knownChannels: [ChannelDescriptor] {
        var seen: [String: ChannelDescriptor] = [:]
        for ext in Self.decoders.values.joined() { seen[ext.descriptor.id] = ext.descriptor }
        for (id, desc) in channels { seen[id] = desc }
        return seen.values.sorted { $0.label < $1.label }
    }

    // Look up a descriptor by channel ID from the decoder registry or discovered channels.
    func descriptor(for id: String) -> ChannelDescriptor? {
        for ext in Self.decoders.values.joined() where ext.descriptor.id == id {
            return ext.descriptor
        }
        return channels[id]
    }

    private var sessionLog:         [(ts: Date, id: String, val: Double)] = []
    private var sessionSum:         [String: Double] = [:]
    private var sessionCount:       [String: Int]    = [:]
    private var sessionStart:       Date?
    private var cancellables        = Set<AnyCancellable>()
    private var timerCancel:        AnyCancellable?
    private var uiRefreshCancel:    AnyCancellable?
    private var lastHistoryWrite:   [String: Date]   = [:]
    // Minimum seconds between history writes; controls max stored resolution.
    // 1.0 → at most 3600 pts/channel over a 1-hour window.
    private static let historyHz:   TimeInterval = 1.0

    // "graphConfigs_v1" JSON used LogChannel rawValue strings (e.g. "rpm") — identical
    // to ChannelDescriptor.id values, so existing saved configs decode without migration.
    private static let graphsKey = "graphConfigs_v1"
    private static let defaultGraphs: [GraphConfig] = [
        GraphConfig(id: UUID(), title: "Engine", channels: ["rpm", "tps"],  windowSeconds: 30),
        GraphConfig(id: UUID(), title: "Fuel",   channels: ["afr", "map"],  windowSeconds: 30),
    ]

    init() { loadGraphs() }

    // MARK: - Decoder registry
    //
    // Maps CAN ID → list of channel extractors.
    // To support a new sensor: add an entry here with a ChannelDescriptor static.
    // The channel appears in the dashboard automatically the first time a valid frame arrives.

    private static let decoders: [UInt16: [FrameExtractor]] = [

        0x304: [    // ENGINE_DATA: [rpm_lo, rpm_hi]
            FrameExtractor(descriptor: .rpm, minDLC: 2) { data in
                Double(UInt16(data[0]) | UInt16(data[1]) << 8)
            },
        ],

        0x306: [    // WBO2_DATA: [afr_lo, afr_hi] — AFR × 100
            FrameExtractor(descriptor: .afr, minDLC: 2) { data in
                Double(UInt16(data[0]) | UInt16(data[1]) << 8) / 100.0
            },
        ],

        0x307: [    // ECU_DATA: [mode, map_kpa, tps_pct, clt_enc, iat_enc, ...]
            FrameExtractor(descriptor: .map,      minDLC: 2) { data in Double(data[1]) },
            FrameExtractor(descriptor: .tps,      minDLC: 3) { data in Double(data[2]) },
            FrameExtractor(descriptor: .coolantC, minDLC: 4) { data in Double(data[3]) - 40 },
            FrameExtractor(descriptor: .intakeC,  minDLC: 5) { data in Double(data[4]) - 40 },
        ],

        0x300: [    // TELEMETRY: [vbat_cv_lo, vbat_cv_hi, i_da_lo, i_da_hi, vsol_cv_lo, vsol_cv_hi, flags, _]
            FrameExtractor(descriptor: .batteryV, minDLC: 2) { data in
                Double(UInt16(data[0]) | UInt16(data[1]) << 8) / 100.0
            },
            FrameExtractor(descriptor: .auxBatteryV, minDLC: 6) { data in
                let v = Double(UInt16(data[4]) | UInt16(data[5]) << 8) / 100.0
                return v > 0 ? v : nil     // 0 = no aux battery connected
            },
        ],

        0x305: [    // GPS_DATA: [speed_lo, speed_hi, heading_lo, heading_hi, flags]
            FrameExtractor(descriptor: .speed, minDLC: 5) { data in
                guard data[4] & 0x02 != 0 else { return nil }   // bit 1 = speed valid
                return Double(UInt16(data[0]) | UInt16(data[1]) << 8) / 10.0
            },
        ],
    ]

    // Maps CHAN_ID_* firmware constants to ChannelDescriptors.
    // Used to decode CHAN_CAP (0x321) frames received from nodes.
    private static let standardChannels: [UInt8: ChannelDescriptor] = [
        0x01: .rpm,
        0x02: .tps,
        0x03: .map,
        0x04: .coolantC,
        0x05: .intakeC,
        0x06: .afr,
        0x07: .speed,
        0x08: .batteryV,
        0x09: .auxBatteryV,
    ]

    // Channel IDs already handled by the static decoder registry — dynamic decoders
    // skip these so the same channel isn't recorded twice.
    private static let staticChannelIDs: Set<String> = {
        var ids = Set<String>()
        for extractors in decoders.values {
            for ext in extractors { ids.insert(ext.descriptor.id) }
        }
        return ids
    }()

    // MARK: - Binding

    func bind(to ble: BLEManager) {
        cancellables = []
        ble.framePublisher
            .receive(on: DispatchQueue.main)
            .sink { [weak self] in self?.ingest($0) }
            .store(in: &cancellables)

        // Drive view updates at a fixed 10 Hz instead of on every incoming frame.
        // current and history are updated at full frame rate; views read fresh values
        // each time the timer fires.
        uiRefreshCancel = Timer.publish(every: 0.1, on: .main, in: .common)
            .autoconnect()
            .sink { [weak self] _ in self?.objectWillChange.send() }
    }

    // MARK: - Frame ingestion

    private func ingest(_ frame: CANFrame) {
        // CHAN_CAP frames teach us how to decode future frames from other nodes.
        if frame.canID == 0x321 { handleChanCap(frame); return }

        var handledIDs = Set<String>()

        // Static decoders — compiled-in, always authoritative.
        if let extractors = Self.decoders[frame.canID] {
            for ext in extractors {
                guard frame.dlc >= ext.minDLC,
                      let value = ext.extract(frame.data) else { continue }
                handledIDs.insert(ext.descriptor.id)
                if channels[ext.descriptor.id] == nil { channels[ext.descriptor.id] = ext.descriptor }
                record(ext.descriptor, value: value)
            }
        }

        // Dynamic decoders — built from CHAN_CAP frames; skip any already handled above.
        if let extractors = dynamicDecoders[frame.canID] {
            for ext in extractors {
                guard !handledIDs.contains(ext.descriptor.id),
                      frame.dlc >= ext.minDLC,
                      let value = ext.extract(frame.data) else { continue }
                if channels[ext.descriptor.id] == nil { channels[ext.descriptor.id] = ext.descriptor }
                record(ext.descriptor, value: value)
            }
        }
    }

    // Build a FrameExtractor from a CHAN_CAP (0x321) frame and register it in
    // dynamicDecoders. Skips channels already covered by the static decoder registry.
    private func handleChanCap(_ frame: CANFrame) {
        guard frame.dlc >= 8 else { return }
        let d          = frame.data
        let chanID     = d[1]
        let srcCanID   = UInt16(d[2]) | UInt16(d[3]) << 8
        let byteOffset = Int(d[4])
        let encoding   = d[5]
        let intOffset  = Int8(bitPattern: d[6])
        let validSpec  = d[7]

        guard let descriptor = Self.standardChannels[chanID] else { return }
        guard !Self.staticChannelIDs.contains(descriptor.id) else { return }

        let typeCode  = Int((encoding >> 6) & 0x03)
        let scaleCode = Int((encoding >> 4) & 0x03)
        let hasOffset = (encoding & 0x08) != 0
        let hasValid  = (encoding & 0x04) != 0
        let skipZero  = (encoding & 0x02) != 0

        let divisor: Double
        switch scaleCode {
        case 1:  divisor = 10.0
        case 2:  divisor = 100.0
        case 3:  divisor = 1000.0
        default: divisor = 1.0
        }

        let addOffset  = hasOffset ? Double(intOffset) : 0.0
        let validByte  = hasValid ? Int(validSpec >> 4) : 0
        let validBit   = hasValid ? Int(validSpec & 0x0F) : 0

        let minDLC: UInt8
        switch typeCode {
        case 0, 2: minDLC = UInt8(hasValid ? max(byteOffset + 1, validByte + 1) : byteOffset + 1)
        default:   minDLC = UInt8(hasValid ? max(byteOffset + 2, validByte + 1) : byteOffset + 2)
        }

        let extractor = FrameExtractor(descriptor: descriptor, minDLC: minDLC) { bytes in
            if hasValid {
                guard validByte < bytes.count,
                      (bytes[validByte] >> validBit) & 1 != 0 else { return nil }
            }
            let raw: Double
            switch typeCode {
            case 0:
                guard byteOffset < bytes.count else { return nil }
                raw = Double(bytes[byteOffset])
            case 1:
                guard byteOffset + 1 < bytes.count else { return nil }
                raw = Double(UInt16(bytes[byteOffset]) | UInt16(bytes[byteOffset + 1]) << 8)
            case 2:
                guard byteOffset < bytes.count else { return nil }
                raw = Double(Int8(bitPattern: bytes[byteOffset]))
            default:
                guard byteOffset + 1 < bytes.count else { return nil }
                raw = Double(Int16(bitPattern: UInt16(bytes[byteOffset]) | UInt16(bytes[byteOffset + 1]) << 8))
            }
            let value = raw / divisor + addOffset
            return skipZero && value == 0.0 ? nil : value
        }

        var extractors = dynamicDecoders[srcCanID] ?? []
        guard !extractors.contains(where: { $0.descriptor.id == descriptor.id }) else { return }
        extractors.append(extractor)
        dynamicDecoders[srcCanID] = extractors
    }

    private func record(_ descriptor: ChannelDescriptor, value: Double) {
        let now = Date()
        let id  = descriptor.id

        current[id] = value

        let last = lastHistoryWrite[id]
        if last == nil || now.timeIntervalSince(last!) >= Self.historyHz {
            lastHistoryWrite[id] = now
            let pt  = DataPoint(timestamp: now, value: value, normalized: descriptor.normalized(value))
            let cut = now.addingTimeInterval(-historyWindow)
            var pts = history[id] ?? []
            pts.append(pt)
            if let firstKeep = pts.firstIndex(where: { $0.timestamp >= cut }) {
                pts = Array(pts[firstKeep...])
            }
            history[id] = pts
        }

        guard isRecording else { return }

        sessionLog.append((ts: now, id: id, val: value))
        sessionMin[id] = sessionMin[id].map { min($0, value) } ?? value
        sessionMax[id] = sessionMax[id].map { max($0, value) } ?? value
        let sum          = (sessionSum[id] ?? 0) + value
        let cnt          = (sessionCount[id] ?? 0) + 1
        sessionSum[id]   = sum
        sessionCount[id] = cnt
        sessionAvg[id]   = sum / Double(cnt)
    }

    // MARK: - Recording

    func startRecording() {
        guard !isRecording else { return }
        sessionStart    = Date()
        sessionLog      = []
        sessionMin      = [:]
        sessionMax      = [:]
        sessionSum      = [:]
        sessionCount    = [:]
        sessionAvg      = [:]
        sessionDuration = 0
        isRecording     = true
        timerCancel = Timer.publish(every: 1, on: .main, in: .common)
            .autoconnect()
            .sink { [weak self] _ in
                guard let self, let start = self.sessionStart else { return }
                self.sessionDuration = Date().timeIntervalSince(start)
            }
    }

    func stopRecording() {
        isRecording = false
        timerCancel = nil
    }

    // MARK: - CSV export

    func exportCSV() -> URL? {
        guard !sessionLog.isEmpty else { return nil }

        // Ordered channel list preserving first-seen order during the session.
        var seen = [String]()
        for entry in sessionLog where !seen.contains(entry.id) { seen.append(entry.id) }
        let activeChannels = seen.compactMap { descriptor(for: $0) }

        var csv  = "Timestamp,"
        csv     += activeChannels.map { "\($0.label)_\($0.unit)" }.joined(separator: ",")
        csv     += "\n"

        var buckets: [Int64: (ts: Date, vals: [String: Double])] = [:]
        for entry in sessionLog {
            let key = Int64(entry.ts.timeIntervalSince1970 * 10)
            var bucket = buckets[key] ?? (ts: entry.ts, vals: [:])
            bucket.vals[entry.id] = entry.val
            buckets[key] = bucket
        }

        let iso = ISO8601DateFormatter()
        iso.formatOptions = [.withInternetDateTime, .withFractionalSeconds]

        for key in buckets.keys.sorted() {
            let b   = buckets[key]!
            let row = [iso.string(from: b.ts)] + activeChannels.map { desc in
                b.vals[desc.id].map { String(format: desc.format, $0) } ?? ""
            }
            csv += row.joined(separator: ",") + "\n"
        }

        let start = sessionStart ?? Date()
        let name  = "TrimLog_\(iso.string(from: start)).csv"
            .replacingOccurrences(of: ":", with: "-")
        let url   = FileManager.default.temporaryDirectory.appendingPathComponent(name)
        try? csv.write(to: url, atomically: true, encoding: .utf8)
        return url
    }

    // MARK: - Graph persistence

    private func saveGraphs() {
        guard let data = try? JSONEncoder().encode(graphs) else { return }
        UserDefaults.standard.set(data, forKey: Self.graphsKey)
    }

    private func loadGraphs() {
        if let data   = UserDefaults.standard.data(forKey: Self.graphsKey),
           let saved  = try? JSONDecoder().decode([GraphConfig].self, from: data),
           !saved.isEmpty {
            graphs = saved
        } else {
            graphs = Self.defaultGraphs
        }
    }
}
