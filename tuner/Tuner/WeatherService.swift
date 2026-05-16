import Foundation
import CoreLocation

final class WeatherService: ObservableObject {
    @Published var temperatureF: Double?
    @Published var temperatureC: Double?
    @Published var humidity: Double?

    private var lastCoord: CLLocationCoordinate2D?
    private var lastFetchDate: Date?

    func fetchIfNeeded(for coordinate: CLLocationCoordinate2D) {
        if let lastDate = lastFetchDate, Date().timeIntervalSince(lastDate) < 300,
           let last = lastCoord {
            let dist = CLLocation(latitude: coordinate.latitude, longitude: coordinate.longitude)
                .distance(from: CLLocation(latitude: last.latitude, longitude: last.longitude))
            if dist < 5_000 { return }
        }
        fetch(for: coordinate)
    }

    func fetch(for coordinate: CLLocationCoordinate2D) {
        lastCoord     = coordinate
        lastFetchDate = Date()
        let urlStr = "https://api.open-meteo.com/v1/forecast"
            + "?latitude=\(coordinate.latitude)"
            + "&longitude=\(coordinate.longitude)"
            + "&current=temperature_2m,relative_humidity_2m"
            + "&temperature_unit=fahrenheit"
        guard let url = URL(string: urlStr) else { return }
        URLSession.shared.dataTask(with: url) { [weak self] data, _, _ in
            guard let data,
                  let json    = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let current = json["current"] as? [String: Any] else { return }
            let tempF = current["temperature_2m"]       as? Double
            let hum   = current["relative_humidity_2m"] as? Double
            DispatchQueue.main.async {
                self?.temperatureF = tempF
                self?.temperatureC = tempF.map { ($0 - 32) * 5 / 9 }
                self?.humidity     = hum
            }
        }.resume()
    }
}
