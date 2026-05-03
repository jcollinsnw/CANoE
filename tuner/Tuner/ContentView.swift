import SwiftUI
import Combine

// MARK: - Root

struct ContentView: View {
    @StateObject private var ble      = BLEManager()
    @StateObject private var location = LocationManager()

    @AppStorage("lastNodeIP") private var savedIP = "192.168.4.1"
    @State private var draftIP       = ""
    @State private var wifiConnected = false

    var body: some View {
        Group {
            if ble.isReady {
                NativeDashboardView(ble: ble, location: location, savedIP: savedIP) {
                    ble.disconnect()
                }
            } else if wifiConnected {
                NodeBrowserView(nodeIP: savedIP, location: location) {
                    wifiConnected = false
                }
            } else {
                ConnectView(nodeIP: $draftIP, ble: ble) {
                    savedIP      = draftIP
                    wifiConnected = true
                }
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

    var body: some View {
        VStack(spacing: 0) {
            Spacer()

            // Icon + title
            VStack(spacing: 10) {
                Image(systemName: "car.fill")
                    .font(.system(size: 52))
                    .foregroundStyle(.green)
                Text("AccessoryBus Tuner")
                    .font(.title2.bold())
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
                    if ble.connectionState == .error("") ||
                       ![BLEManager.ConnectionState.scanning,
                         .connecting, .discovering].contains(ble.connectionState) {
                        // handled below
                    }
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
                    .keyboardType(.decimalPad)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                    .font(.system(.body, design: .monospaced))
                    .onSubmit(onWifiConnect)

                Text("Join \"Crustang\" Wi-Fi first, then tap Connect.")
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
    @ObservedObject var ble:      BLEManager
    @ObservedObject var location: LocationManager
    let savedIP: String
    let onDisconnect: () -> Void

    @State private var showWebConsole = false

    private static let timeFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.SSS"
        return f
    }()

    var body: some View {
        NavigationStack {
            ZStack(alignment: .bottom) {
                // Frame log
                Color(red: 0.04, green: 0.06, blue: 0.08)
                    .ignoresSafeArea()

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
                        .padding(.bottom, 72)   // clear the "Web Console" button
                    }
                    .onChange(of: ble.frames.count) { _ in
                        if let last = ble.frames.last {
                            withAnimation { proxy.scrollTo(last.id, anchor: .bottom) }
                        }
                    }
                }

                // "Open Web Console" floating button
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
                .padding(.bottom, 16)
                .disabled(savedIP.isEmpty)
            }
            .navigationTitle(ble.peripheralName.isEmpty ? "CAN Bus" : ble.peripheralName)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button("Disconnect", systemImage: "xmark") {
                        location.stop()
                        onDisconnect()
                    }
                    .tint(.red)
                }
                ToolbarItem(placement: .topBarTrailing) {
                    HStack(spacing: 12) {
                        Button(systemImage: "trash") { ble.clearFrames() }
                            .tint(.secondary)
                        GpsToggleButton(location: location)
                    }
                }
            }
        }
        // Forward GPS frames to the ESP32 via BLE while the dashboard is visible.
        .onReceive(location.$latestFrame.compactMap { $0 }) { frame in
            ble.sendFrame(id: 0x305, data: frame)
        }
        // Auto-start GPS as soon as the dashboard appears (BLE just connected).
        // The user can still stop/start manually with the GPS toolbar button.
        .onAppear {
            UIApplication.shared.isIdleTimerDisabled = true
            if !location.isRunning { location.start() }
        }
        .onDisappear { UIApplication.shared.isIdleTimerDisabled = false }
        .sheet(isPresented: $showWebConsole) {
            NodeBrowserView(nodeIP: savedIP, location: location,
                            closeLabel: "Close") {
                showWebConsole = false
            }
        }
        .alert("Location Access Denied", isPresented: $location.authDenied) {
            Button("Open Settings") {
                if let url = URL(string: UIApplication.openSettingsURLString) {
                    UIApplication.shared.open(url)
                }
            }
            Button("Cancel", role: .cancel) {}
        } message: {
            Text("Enable location access in Settings so the app can publish phone GPS to the CAN bus.")
        }
    }
}

// MARK: - CAN frame row (used in NativeDashboardView log)

private struct CANFrameRow: View {
    let frame:     CANFrame
    let formatter: DateFormatter

    var body: some View {
        Text(line)
            .font(.system(size: 12, design: .monospaced))
            .foregroundStyle(frame.isOutbound
                ? Color(red: 0.6,  green: 0.84, blue: 0.44)   // green — TX
                : Color(red: 0.72, green: 0.82, blue: 0.95))  // blue  — RX
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
                .onAppear   { UIApplication.shared.isIdleTimerDisabled = true  }
                .onDisappear { UIApplication.shared.isIdleTimerDisabled = false }
                // Forward GPS frames to the web UI via JavaScript.
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
            Button("Open Settings") {
                if let url = URL(string: UIApplication.openSettingsURLString) {
                    UIApplication.shared.open(url)
                }
            }
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
    ConnectView(nodeIP: .constant("192.168.4.1"), ble: BLEManager()) {}
}
