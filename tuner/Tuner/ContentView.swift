import SwiftUI
import Combine

// MARK: - Root

struct ContentView: View {
    @StateObject private var ble       = BLEManager()
    @StateObject private var location  = LocationManager()
    @StateObject private var weather   = WeatherService()
    @StateObject private var altimeter = AltimeterManager()
    @StateObject private var logger    = DataLogger()
    @StateObject private var simulator = SimulatorDriver()

    @AppStorage("lastNodeIP") private var savedIP = "192.168.4.1"
    @State private var draftIP       = ""
    @State private var wifiConnected = false

    var body: some View {
        Group {
            if ble.isReady || simulator.isRunning {
                NativeDashboardView(
                    ble:       ble,
                    location:  location,
                    weather:   weather,
                    altimeter: altimeter,
                    logger:    logger,
                    simulator: simulator,
                    savedIP:   savedIP
                ) {
                    location.stop()
                    altimeter.stop()
                    simulator.stop()
                    ble.disconnect()
                }
            } else if wifiConnected {
                NodeBrowserView(nodeIP: savedIP, location: location) {
                    wifiConnected = false
                }
            } else {
                ConnectView(nodeIP: $draftIP, ble: ble,
                            onWifiConnect: {
                                savedIP       = draftIP
                                wifiConnected = true
                            },
                            onSimulate: {
                                simulator.start(ble: ble)
                            })
                .onAppear { draftIP = savedIP }
            }
        }
    }
}

// MARK: - Connect screen

struct ConnectView: View {
    @Binding var nodeIP: String
    @ObservedObject var ble: BLEManager
    let onWifiConnect: () -> Void
    let onSimulate:    () -> Void

    var body: some View {
        VStack(spacing: 0) {
            Spacer()

            // Logo + wordmark
            VStack(spacing: 14) {
                Image("canoe")
                    .resizable()
                    .scaledToFit()
                    .frame(height: 110)
                Text("Trim")
                    .font(.custom("Futura-Medium", size: 48))
                    .tracking(6)
                    .foregroundStyle(.primary)
                Text("Connect to a CAN bus node")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }
            .padding(.bottom, 36)

            // ── Bluetooth card ────────────────────────────────────────────
            VStack(alignment: .leading, spacing: 10) {
                Label("BLUETOOTH", systemImage: "antenna.radiowaves.left.and.right")
                    .font(.caption.bold())
                    .foregroundStyle(.secondary)
                    .textCase(.uppercase)

                HStack {
                    Circle()
                        .fill(ble.connectionState == .scanning ? Color.blue : .secondary.opacity(0.4))
                        .frame(width: 8, height: 8)
                    Text(ble.connectionState.label)
                        .font(.system(.subheadline, design: .monospaced))
                        .foregroundStyle(ble.connectionState.isActive ? .primary : .secondary)
                    Spacer()
                }

                Button {
                    if ble.connectionState == .scanning {
                        ble.stopScan()
                    } else {
                        ble.startScan()
                    }
                } label: {
                    Label(
                        ble.connectionState == .scanning ? "Stop Scanning" : "Scan for CAN Bus",
                        systemImage: ble.connectionState == .scanning ? "stop.circle" : "magnifyingglass"
                    )
                    .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .tint(.blue)
                .disabled([.connecting, .discovering].contains(ble.connectionState))

                Text("Bluetooth auto-connects to the nearest node advertising the CAN bus service.")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
            .padding()
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
            .padding(.horizontal)

            Spacer().frame(height: 16)

            // ── WiFi / Web UI card ────────────────────────────────────────
            VStack(alignment: .leading, spacing: 6) {
                Label("WEB CONSOLE (WI-FI)", systemImage: "network")
                    .font(.caption.bold())
                    .foregroundStyle(.secondary)
                    .textCase(.uppercase)

                TextField("192.168.4.1", text: $nodeIP)
                    .textFieldStyle(.roundedBorder)
#if os(iOS)
                    .keyboardType(.decimalPad)
#endif
                    .autocorrectionDisabled()
#if os(iOS)
                    .textInputAutocapitalization(.never)
#endif
                    .font(.system(.body, design: .monospaced))
                    .onSubmit(onWifiConnect)

                Text("Join \"Crustang\" Wi-Fi first, then tap Connect.")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
            .padding()
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
            .padding(.horizontal)

            Spacer().frame(height: 16)

            // ── Simulator card ────────────────────────────────────────────
            VStack(alignment: .leading, spacing: 8) {
                Label("SIMULATOR", systemImage: "waveform.path")
                    .font(.caption.bold())
                    .foregroundStyle(.secondary)
                    .textCase(.uppercase)

                Button(action: onSimulate) {
                    Label("Run Simulator", systemImage: "play.circle.fill")
                        .font(.body.bold())
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 10)
                }
                .buttonStyle(.borderedProminent)
                .tint(.orange)

                Text("Generates realistic engine data for testing without hardware.")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
            .padding()
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
            .padding(.horizontal)

            Spacer().frame(height: 24)

            // Web UI connect button
            Button(action: onWifiConnect) {
                Label("Open Web Console", systemImage: "globe")
                    .font(.body.bold())
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 14)
            }
            .buttonStyle(.borderedProminent)
            .tint(.green)
            .padding(.horizontal)
            .disabled(nodeIP.trimmingCharacters(in: .whitespaces).isEmpty)

            Spacer()
        }
        .background(Color(.systemGroupedBackground).ignoresSafeArea())
    }
}

// MARK: - Native BLE dashboard

struct NativeDashboardView: View {
    @ObservedObject var ble:       BLEManager
    @ObservedObject var location:  LocationManager
    @ObservedObject var weather:   WeatherService
    @ObservedObject var altimeter: AltimeterManager
    @ObservedObject var logger:    DataLogger
    @ObservedObject var simulator: SimulatorDriver
    let savedIP: String
    let onDisconnect: () -> Void

    @State private var showWebConsole  = false
    @State private var selectedTab     = 0
    @State private var framesCSVURL:   URL?   = nil
    @State private var showFramesShare = false

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                WeatherStrip(weather: weather, altimeter: altimeter)

                TabView(selection: $selectedTab) {

                    // ── Frames tab ──────────────────────────────────────────
                    ZStack(alignment: .bottom) {
                        FrameLogView(ble: ble)

                        HStack(alignment: .bottom) {
                            Button {
                                if let url = ble.exportFramesCSV() {
                                    framesCSVURL    = url
                                    showFramesShare = true
                                }
                            } label: {
                                Label("Export CSV", systemImage: "square.and.arrow.up")
                                    .font(.subheadline.bold())
                                    .padding(.vertical, 12)
                                    .padding(.horizontal, 20)
                                    .background(.regularMaterial, in: Capsule())
                                    .shadow(radius: 4)
                            }
                            .disabled(ble.frames.isEmpty)
                            .opacity(ble.frames.isEmpty ? 0.4 : 1.0)

                            Spacer()

                            if !savedIP.isEmpty {
                                Button {
                                    showWebConsole = true
                                } label: {
                                    Label("Open Web Console", systemImage: "globe")
                                        .font(.subheadline.bold())
                                        .padding(.vertical, 12)
                                        .padding(.horizontal, 20)
                                        .background(.regularMaterial, in: Capsule())
                                        .shadow(radius: 4)
                                }
                            }
                        }
                        .padding(.horizontal, 16)
                        .padding(.bottom, 16)
                    }
                    .tabItem { Label("Frames", systemImage: "waveform") }
                    .tag(0)

                    // ── Logger tab ──────────────────────────────────────────
                    LogView(logger: logger)
                        .tabItem { Label("Logger", systemImage: "chart.xyaxis.line") }
                        .tag(1)
                }
                .tint(.green)
            }
            .background(Color(.systemBackground).ignoresSafeArea())
            .navigationTitle(navTitle)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading)  { disconnectButton }
                ToolbarItem(placement: .topBarTrailing) { trailingButtons }
            }
        }
        .onReceive(location.$latestFrame.compactMap { $0 }) { frame in
            ble.sendFrame(id: 0x305, data: frame)
        }
        .onReceive(location.$coordinate.compactMap { $0 }) { coord in
            weather.fetchIfNeeded(for: coord)
        }
        .onReceive(weather.$temperatureC.compactMap { $0 }) { _ in
            sendEnvData()
        }
        .onReceive(Timer.publish(every: 30, on: .main, in: .common).autoconnect()) { _ in
            sendEnvData()
        }
        .onAppear {
#if os(iOS)
            UIApplication.shared.isIdleTimerDisabled = true
#endif
            logger.bind(to: ble)
            // Ask all nodes what channels they publish — responses build dynamic decoders.
            if ble.isReady { ble.sendFrame(id: 0x320, data: [0xFF]) }
            if !location.isRunning { location.start() }
            altimeter.start()
            if let coord = location.coordinate { weather.fetch(for: coord) }
        }
        .onDisappear {
#if os(iOS)
            UIApplication.shared.isIdleTimerDisabled = false
#endif
            altimeter.stop()
        }
        .sheet(isPresented: $showFramesShare) {
            if let url = framesCSVURL {
                ShareSheet(url: url)
            }
        }
        .sheet(isPresented: $showWebConsole) {
            NodeBrowserView(nodeIP: savedIP, location: location, closeLabel: "Close") {
                showWebConsole = false
            }
        }
        .alert("Location Access Denied", isPresented: $location.authDenied) {
#if os(iOS)
            Button("Open Settings") {
                if let url = URL(string: UIApplication.openSettingsURLString) {
                    UIApplication.shared.open(url)
                }
            }
#endif
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Enable location access in Settings so the app can publish phone GPS to the CAN bus.")
        }
    }

    private var navTitle: String {
        if simulator.isRunning { return "Simulator" }
        return ble.peripheralName.isEmpty ? "CAN Bus" : ble.peripheralName
    }

    @ViewBuilder private var disconnectButton: some View {
        Button("Disconnect", systemImage: "xmark") {
            location.stop()
            altimeter.stop()
            onDisconnect()
        }
        .tint(.red)
    }

    @ViewBuilder private var trailingButtons: some View {
        HStack(spacing: 12) {
            if simulator.isRunning {
                Button {
                    simulator.stop()
                } label: {
                    Label("Stop Sim", systemImage: "stop.circle.fill")
                        .foregroundStyle(.orange)
                }
            }
            Button(action: { ble.clearFrames() }) {
                Image(systemName: "trash")
            }
            .tint(.secondary)
#if os(iOS)
            if !simulator.isRunning {
                GpsToggleButton(location: location)
            }
#endif
        }
    }

    private func sendEnvData() {
        guard let tempC = weather.temperatureC else { return }
        let raw   = Int16(clamping: Int((tempC * 10).rounded()))
        let bytes = withUnsafeBytes(of: raw.littleEndian) { Array($0) }
        ble.sendFrame(id: 0x301, data: [bytes[0], bytes[1], 0x00, 0x00])
    }
}

// MARK: - Frame log

private struct FrameLogView: View {
    @ObservedObject var ble: BLEManager

    private static let timeFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f
    }()

    var body: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 1) {
                    ForEach(ble.frames) { frame in
                        CANFrameRow(frame: frame, formatter: Self.timeFormatter)
                            .id(frame.id)
                    }
                }
                .padding(.horizontal, 8)
                .padding(.top, 8)
                .padding(.bottom, 72)
            }
            .onChange(of: ble.frames.count) { _ in
                if let last = ble.frames.last {
                    withAnimation { proxy.scrollTo(last.id, anchor: .bottom) }
                }
            }
        }
    }
}

// MARK: - Weather strip

private struct WeatherStrip: View {
    @ObservedObject var weather:   WeatherService
    @ObservedObject var altimeter: AltimeterManager

    var body: some View {
        HStack(spacing: 20) {
            item("thermometer", weather.temperatureF.map { String(format: "%.1f°F", $0) } ?? "—")
            item("drop.fill",   weather.humidity.map    { String(format: "%.0f%%",  $0) } ?? "—")
            item("gauge",       altimeter.pressureHPa.map { String(format: "%.0f hPa", $0) } ?? "—")
            Spacer()
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(Color(.secondarySystemBackground))
    }

    private func item(_ icon: String, _ label: String) -> some View {
        Label(label, systemImage: icon)
            .font(.system(size: 11, design: .monospaced))
            .foregroundStyle(.secondary)
    }
}

// MARK: - CAN frame row

private struct CANFrameRow: View {
    let frame:     CANFrame
    let formatter: DateFormatter

    var body: some View {
        Text(line)
            .font(.system(size: 12, design: .monospaced))
            .foregroundStyle(frame.isOutbound ? Color.green : Color.blue)
            .lineLimit(1)
    }

    private var line: String {
        let ts  = formatter.string(from: frame.timestamp)
        let dir = frame.isOutbound ? "TX" : "RX"
        return "\(ts)  \(dir)  \(frame.idHex)  [\(frame.dlc)]  \(frame.dataHex)"
    }
}

// MARK: - Web console (wraps the node web UI)

struct NodeBrowserView: View {
    let nodeIP:    String
    @ObservedObject var location: LocationManager
    var closeLabel: String = "Disconnect"
    let onDisconnect: () -> Void

    @State private var webCoordinator: NodeWebView.Coordinator?

    var nodeURL: URL { URL(string: "http://\(nodeIP)/")! }

    var body: some View {
        NavigationStack {
            NodeWebView(url: nodeURL, onCoordinatorReady: { webCoordinator = $0 })
                .ignoresSafeArea(edges: .bottom)
                .onAppear {
#if os(iOS)
                    UIApplication.shared.isIdleTimerDisabled = true
#endif
                }
                .onDisappear {
#if os(iOS)
                    UIApplication.shared.isIdleTimerDisabled = false
#endif
                }
                .onReceive(location.$latestFrame.compactMap { $0 }) { frame in
                    webCoordinator?.sendGpsFrame(frame)
                }
                .navigationTitle(nodeIP)
                .navigationBarTitleDisplayMode(.inline)
                .toolbar {
                    ToolbarItem(placement: .topBarLeading) {
                        Button(closeLabel, systemImage: "xmark") {
                            location.stop()
                            onDisconnect()
                        }
                        .tint(.red)
                    }
                    ToolbarItem(placement: .topBarTrailing) {
                        GpsToggleButton(location: location)
                    }
                }
        }
        .alert("Location Access Denied", isPresented: $location.authDenied) {
#if os(iOS)
            Button("Open Settings") {
                if let url = URL(string: UIApplication.openSettingsURLString) {
                    UIApplication.shared.open(url)
                }
            }
#endif
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Enable location access in Settings so the app can publish phone speed to the CAN bus.")
        }
    }
}

// MARK: - GPS toolbar button

struct GpsToggleButton: View {
    @ObservedObject var location: LocationManager

    var body: some View {
        Button {
            location.isRunning ? location.stop() : location.start()
        } label: {
            Label(
                location.isRunning ? "GPS On" : "Phone GPS",
                systemImage: location.isRunning ? "location.fill" : "location"
            )
            .foregroundStyle(location.isRunning ? .green : .secondary)
        }
    }
}

// MARK: - Previews

#Preview("Connect") {
    ConnectView(nodeIP: .constant("192.168.4.1"), ble: BLEManager(),
                onWifiConnect: {}, onSimulate: {})
}
