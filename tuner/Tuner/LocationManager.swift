import CoreLocation
import Combine

/// Wraps CLLocationManager and publishes speed/heading suitable for encoding
/// as a CAN GPS_DATA (0x305) frame identical to what the hardware GPS module sends.
///
/// Consumers observe `latestFrame` — a ready-to-send [UInt8] payload — whenever
/// a new fix arrives with at least one valid field.
final class LocationManager: NSObject, ObservableObject, CLLocationManagerDelegate {

    // Published state
    @Published var isRunning   = false
    @Published var authDenied  = false
    @Published var speedMph: Double?                    // nil until first valid fix
    @Published var headingDeg: Double?                  // nil until first valid heading
    @Published var coordinate: CLLocationCoordinate2D?  // nil until first fix

    /// Latest 5-byte GPS_DATA payload: [spd_lo, spd_hi, hdg_lo, hdg_hi, flags]
    /// Matches CAN_ID_GPS_DATA (0x305) format from the hardware GPS module.
    @Published var latestFrame: [UInt8]?

    private let manager = CLLocationManager()

    override init() {
        super.init()
        manager.delegate            = self
        manager.desiredAccuracy     = kCLLocationAccuracyBestForNavigation
        manager.distanceFilter      = kCLDistanceFilterNone
        manager.headingFilter       = 1.0            // update every 1°
        manager.activityType        = .automotiveNavigation
        manager.pausesLocationUpdatesAutomatically  = false
        // Keep delivering location when the app is backgrounded (blue status bar
        // is shown on device). Required for GPS→CAN injection while screen is off.
        // macOS does not have this property.
#if os(iOS)
        manager.allowsBackgroundLocationUpdates = true
#endif
    }

    func start() {
        switch manager.authorizationStatus {
        case .notDetermined:
            manager.requestWhenInUseAuthorization()
        case .denied, .restricted:
            authDenied = true
        default:
            beginUpdates()
        }
    }

    func stop() {
        manager.stopUpdatingLocation()
        manager.stopUpdatingHeading()
        isRunning  = false
        latestFrame = nil
    }

    // MARK: - CLLocationManagerDelegate

    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        switch manager.authorizationStatus {
        case .authorizedWhenInUse, .authorizedAlways:
            authDenied = false
            if isRunning { beginUpdates() }
        case .denied, .restricted:
            authDenied = true
            stop()
        default:
            break
        }
    }

    func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        guard let loc = locations.last else { return }

        // Negative speed / heading = invalid on iOS
        let spdMph  = loc.speed   >= 0 ? loc.speed   * 2.23694 : nil  // m/s → mph
        let hdgDeg  = loc.course  >= 0 ? loc.course             : nil  // degrees true

        speedMph   = spdMph
        headingDeg = hdgDeg
        coordinate = loc.coordinate

        buildAndPublish(speed: spdMph, heading: hdgDeg)
    }

    func locationManager(_ manager: CLLocationManager, didUpdateHeading newHeading: CLHeading) {
        // Heading comes from the separate heading stream; merge with last speed.
        let hdg = newHeading.trueHeading >= 0 ? newHeading.trueHeading : nil
        headingDeg = hdg
        buildAndPublish(speed: speedMph, heading: hdg)
    }

    func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        // Non-fatal — just let the last published frame age out.
        print("[LocationManager] error: \(error.localizedDescription)")
    }

    // MARK: - Private

    private func beginUpdates() {
        manager.startUpdatingLocation()
        if CLLocationManager.headingAvailable() {
            manager.startUpdatingHeading()
        }
        isRunning = true
    }

    /// Build the 5-byte 0x305 payload and publish it.
    ///
    /// Encoding (matching hardware GPS module):
    ///   speed   — uint16 LE, mph × 10
    ///   heading — uint16 LE, degrees × 10
    ///   flags   — bit0 fix | bit1 speed valid | bit2 heading valid
    private func buildAndPublish(speed: Double?, heading: Double?) {
        let spdRaw = UInt16(clamping: Int((speed   ?? 0) * 10))  // 0.1-mph units
        let hdgRaw = UInt16(clamping: Int((heading ?? 0) * 10))  // 0.1-deg units

        var flags: UInt8 = 0x01                        // bit0 = fix acquired
        if speed   != nil { flags |= 0x02 }           // bit1 = speed valid
        if heading != nil { flags |= 0x04 }           // bit2 = heading valid

        latestFrame = [
            UInt8(spdRaw & 0xFF), UInt8(spdRaw >> 8),
            UInt8(hdgRaw & 0xFF), UInt8(hdgRaw >> 8),
            flags
        ]
    }
}
