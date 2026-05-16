import Foundation
import Combine

// Generates plausible engine telemetry and injects it into BLEManager as fake
// CAN frames. Useful for UI development and demos without real hardware.
//
// State machine: cold-start → warm-idle → accelerate → cruise → decelerate → repeat

final class SimulatorDriver: ObservableObject {
    @Published var isRunning = false

    private weak var ble: BLEManager?
    private var timerCancel: AnyCancellable?

    // Engine state
    private var rpm:       Double = 800
    private var coolantC:  Double = 20.0
    private var targetRPM: Double = 800
    private var tps:       Double = 5.0
    private var phase:     Phase  = .idle
    private var phaseAge:  Double = 0       // seconds in current phase

    private enum Phase { case idle, accelerating, cruising, decelerating }

    func start(ble: BLEManager) {
        guard !isRunning else { return }
        self.ble   = ble
        isRunning  = true
        // Reset to cold state each session
        rpm       = 800
        coolantC  = 20
        tps       = 5
        phase     = .idle
        phaseAge  = 0
        timerCancel = Timer.publish(every: 0.2, on: .main, in: .common)
            .autoconnect()
            .sink { [weak self] _ in self?.tick() }
    }

    func stop() {
        isRunning   = false
        timerCancel = nil
        ble         = nil
    }

    // MARK: - State machine (10 Hz)

    private func tick() {
        phaseAge += 0.2

        // Warm up coolant toward 88 °C (fast for demo: ~0.5 °C/s)
        coolantC = min(88, coolantC + 0.10)

        // Phase transitions
        switch phase {
        case .idle:
            tps = 4 + jitter(1)
            if phaseAge > Double.random(in: 3...6) {
                phase     = .accelerating
                phaseAge  = 0
                targetRPM = Double.random(in: 1800...4500)
            }

        case .accelerating:
            tps = min(75, tps + 2.5)
            if abs(rpm - targetRPM) < 150 || phaseAge > 4 {
                phase    = .cruising
                phaseAge = 0
            }

        case .cruising:
            tps = clamp(targetRPM / 6000 * 55 + jitter(4), lo: 8, hi: 75)
            if phaseAge > Double.random(in: 4...10) {
                phase     = .decelerating
                phaseAge  = 0
                targetRPM = 800
            }

        case .decelerating:
            tps = max(2, tps - 3.5)
            if rpm < 900 || phaseAge > 4 {
                phase    = .idle
                phaseAge = 0
            }
        }

        // Smooth RPM toward target
        let rpmError = targetRPM - rpm
        rpm = clamp(rpm + rpmError * 0.12 + jitter(25), lo: 700, hi: 8000)

        // Derive engine parameters from RPM and TPS
        let load     = tps / 100.0
        let mapKPa   = clamp(101.3 - (101.3 - 28) * load + jitter(1.5), lo: 20, hi: 108)
        let afr      = targetAFR(load: load)
        let iatC     = clamp(35.0 + coolantC * 0.06 + jitter(0.5), lo: -10, hi: 60)
        let battV    = 13.8 + (rpm > 1000 ? 0.25 : 0) + jitter(0.05)
        let auxBattV = 12.4 + (rpm > 1000 ? 0.40 : 0) + jitter(0.04)
        let speedMph = clamp((rpm - 800) / 45.0 + jitter(1), lo: 0, hi: 120)

        // Send as CAN frames
        send(0x304, le16(UInt16(rpm)))
        send(0x307, [0x01,
                     UInt8(clamping: mapKPa),
                     UInt8(clamping: tps),
                     UInt8(clamping: coolantC + 40),
                     UInt8(clamping: iatC + 40),
                     0x00, 0x00, 0x09])
        send(0x306, le16(UInt16(afr * 100)))
        // TELEMETRY: [vbat_cv_lo, vbat_cv_hi, i_da_lo, i_da_hi, vsol_cv_lo, vsol_cv_hi, flags, _]
        send(0x300, le16(UInt16(battV * 100)) + [0x00, 0x00] + le16(UInt16(auxBattV * 100)) + [0x00, 0x00])
        send(0x305, le16(UInt16(speedMph * 10)) + [0x00, 0x00, 0x07])
    }

    // Rich under heavy load, lean at idle, stoich in between
    private func targetAFR(load: Double) -> Double {
        let base: Double
        if load < 0.12      { base = 14.7 }
        else if load > 0.65 { base = 12.3 + load * 0.5 }
        else                { base = 14.7 - load * 3.2 }
        return clamp(base + jitter(0.15), lo: 10.5, hi: 17.5)
    }

    // MARK: - Frame helpers

    private func send(_ canID: UInt16, _ payload: [UInt8]) {
        guard let ble else { return }
        let dlc    = UInt8(min(payload.count, 8))
        let padded = Array(payload.prefix(8)) + Array(repeating: UInt8(0), count: max(0, 8 - payload.count))
        ble.injectFrame(CANFrame(canID: canID, dlc: dlc, data: padded,
                                 timestamp: Date(), isOutbound: false))
    }

    // Encode UInt16 as two little-endian bytes
    private func le16(_ v: UInt16) -> [UInt8] {
        [UInt8(v & 0xFF), UInt8(v >> 8)]
    }

    private func jitter(_ magnitude: Double) -> Double {
        Double.random(in: -magnitude...magnitude)
    }

    private func clamp(_ v: Double, lo: Double, hi: Double) -> Double {
        max(lo, min(hi, v))
    }
}

// MARK: - UInt8 clamping init (avoids overflow traps in simulator)

private extension UInt8 {
    init(clamping v: Double) {
        self = UInt8(Swift.max(0, Swift.min(255, v)))
    }
}
