import CoreMotion

final class AltimeterManager: ObservableObject {
    @Published var pressureHPa: Double?

    private let altimeter = CMAltimeter()

    func start() {
        guard CMAltimeter.isRelativeAltitudeAvailable() else { return }
        altimeter.startRelativeAltitudeUpdates(to: .main) { [weak self] data, error in
            guard let data, error == nil else { return }
            // CMAltitudeData.pressure is in kPa; 1 kPa = 10 hPa
            self?.pressureHPa = data.pressure.doubleValue * 10
        }
    }

    func stop() {
        altimeter.stopRelativeAltitudeUpdates()
    }
}
