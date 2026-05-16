import SwiftUI
import Charts

// MARK: - Channel color (view-layer extension — keeps SwiftUI out of the model)

extension ChannelDescriptor {
    var color: Color {
        switch id {
        case "rpm":         return .red
        case "tps":         return .green
        case "afr":         return .orange
        case "map":         return .purple
        case "coolantC":    return .teal
        case "intakeC":     return Color(hue: 0.12, saturation: 0.9, brightness: 0.75)
        case "batteryV":    return .blue
        case "auxBatteryV": return .indigo
        case "speed":       return Color(red: 0.15, green: 0.65, blue: 0.35)
        default:            return .gray
        }
    }
}

// MARK: - Root log view

struct LogView: View {
    @ObservedObject var logger: DataLogger

    @State private var csvURL:      URL?
    @State private var showShare    = false
    @State private var editingGraph: GraphConfig?

    var body: some View {
        ScrollView {
            LazyVStack(spacing: 12) {
                RecordBar(logger: logger, onExport: prepareExport)
                LiveGaugeGrid(logger: logger)

                ForEach($logger.graphs) { $graph in
                    let onEdit:   () -> Void = { editingGraph = graph }
                    let onDelete: () -> Void = { logger.graphs.removeAll { $0.id == graph.id } }
                    if graph.graphType == .gauge {
                        BraunGaugePanel(graph: $graph, logger: logger,
                                        onEdit: onEdit, onDelete: onDelete)
                    } else {
                        GraphPanel(graph: $graph, logger: logger,
                                   onEdit: onEdit, onDelete: onDelete)
                    }
                }

                addGraphButton

                if !logger.sessionAvg.isEmpty {
                    SessionStatsView(logger: logger)
                }
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 12)
        }
        .background(Color(.systemGroupedBackground).ignoresSafeArea())
        .sheet(item: $editingGraph) { snapshot in
            if let idx = logger.graphs.firstIndex(where: { $0.id == snapshot.id }) {
                GraphEditorSheet(
                    graph:     $logger.graphs[idx],
                    available: logger.knownChannels
                ) {
                    logger.graphs.remove(at: idx)
                    editingGraph = nil
                }
            }
        }
        .sheet(isPresented: $showShare) {
            if let url = csvURL {
                ShareSheet(url: url)
            }
        }
    }

    private var addGraphButton: some View {
        Button {
            logger.graphs.append(
                GraphConfig(title: "New Graph", channels: ["rpm"], windowSeconds: 30)
            )
        } label: {
            Label("Add Graph", systemImage: "plus")
                .font(.subheadline)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 10)
        }
        .buttonStyle(.bordered)
        .tint(.secondary)
    }

    private func prepareExport() {
        guard let url = logger.exportCSV() else { return }
        csvURL   = url
        showShare = true
    }
}

// MARK: - Record bar

private struct RecordBar: View {
    @ObservedObject var logger: DataLogger
    let onExport: () -> Void

    var body: some View {
        HStack(spacing: 12) {
            Button {
                logger.isRecording ? logger.stopRecording() : logger.startRecording()
            } label: {
                HStack(spacing: 8) {
                    Image(systemName: logger.isRecording ? "stop.circle.fill" : "record.circle")
                        .font(.title3)
                    if logger.isRecording {
                        Text(formatDuration(logger.sessionDuration))
                            .font(.system(.body, design: .monospaced))
                        Text("REC")
                            .font(.system(size: 10, weight: .bold, design: .monospaced))
                            .padding(.horizontal, 5).padding(.vertical, 2)
                            .background(.red.opacity(0.15), in: RoundedRectangle(cornerRadius: 4))
                    } else {
                        Text("Record Session")
                            .font(.system(.subheadline, design: .monospaced))
                    }
                }
            }
            .buttonStyle(.bordered)
            .tint(logger.isRecording ? .red : .primary)

            Spacer()

            Button("Export Data CSV", systemImage: "square.and.arrow.up", action: onExport)
                .buttonStyle(.bordered)
                .tint(.green)
                .font(.subheadline)
                .disabled(logger.isRecording || logger.sessionMin.isEmpty)
                .opacity(logger.sessionMin.isEmpty ? 0.4 : 1.0)
        }
        .padding(.horizontal, 4)
        .padding(.vertical, 4)
    }

    private func formatDuration(_ t: TimeInterval) -> String {
        String(format: "%02d:%02d", Int(t) / 60, Int(t) % 60)
    }
}

// MARK: - Live gauge grid
// Only shows channels that have received at least one frame — auto-populates on discovery.

private struct LiveGaugeGrid: View {
    @ObservedObject var logger: DataLogger

    var body: some View {
        LazyVGrid(columns: [GridItem(.adaptive(minimum: 84), spacing: 8)], spacing: 8) {
            ForEach(logger.channels.values.sorted { $0.label < $1.label }) { desc in
                GaugeTile(descriptor: desc, value: logger.current[desc.id])
            }
        }
    }
}

private struct GaugeTile: View {
    let descriptor: ChannelDescriptor
    let value:      Double?

    var body: some View {
        VStack(spacing: 2) {
            Text(descriptor.label)
                .font(.system(size: 9, weight: .medium, design: .monospaced))
                .foregroundStyle(.secondary)
            Group {
                if let v = value {
                    Text(String(format: descriptor.format, v))
                        .font(.system(size: 18, weight: .semibold, design: .monospaced))
                        .foregroundStyle(descriptor.color)
                } else {
                    Text("—")
                        .font(.system(size: 18, weight: .semibold, design: .monospaced))
                        .foregroundStyle(.tertiary)
                }
            }
            Text(descriptor.unit)
                .font(.system(size: 9, design: .monospaced))
                .foregroundStyle(.tertiary)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 8)
        .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 8))
    }
}

// MARK: - Graph panel

private struct GraphPanel: View {
    @Binding var graph:  GraphConfig
    @ObservedObject var logger: DataLogger
    let onEdit:   () -> Void
    let onDelete: () -> Void

    @State private var frozenAt:     Date?
    @State private var cursorDate:   Date?
    @State private var cursorValues: [String: Double] = [:]

    private var isPaused: Bool { frozenAt != nil }

    // Resolve channel IDs → descriptors at render time; unknown IDs are silently dropped.
    private var resolvedChannels: [ChannelDescriptor] {
        graph.channels.compactMap { logger.descriptor(for: $0) }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            headerRow
            if cursorDate != nil { cursorRow }

            TimelineView(.periodic(from: .now, by: 1)) { ctx in
                ChannelChart(
                    channels:   resolvedChannels,
                    history:    logger.history,
                    windowSecs: graph.windowSeconds,
                    now:        frozenAt ?? ctx.date,
                    cursorDate: cursorDate,
                    onDrag:     handleDrag,
                    onDragEnd:  { }
                )
                .frame(height: 130)
            }

            footerRow
        }
        .padding(12)
        .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 12))
    }

    // MARK: Sub-rows

    @ViewBuilder private var headerRow: some View {
        HStack(spacing: 8) {
            Text(graph.title)
                .font(.system(.subheadline, design: .monospaced).bold())

            if isPaused {
                Text("PAUSED")
                    .font(.system(size: 8, weight: .bold, design: .monospaced))
                    .foregroundStyle(.orange)
                    .padding(.horizontal, 4).padding(.vertical, 2)
                    .background(.orange.opacity(0.15), in: RoundedRectangle(cornerRadius: 4))
            }

            Spacer()

            ForEach(resolvedChannels) { desc in valueBadge(desc) }

            Button(action: onEdit) {
                Image(systemName: "slider.horizontal.3").font(.caption)
            }
            .tint(.secondary)
        }
    }

    private static let cursorFormatter: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss.S"
        return f
    }()

    @ViewBuilder private var cursorRow: some View {
        HStack(spacing: 10) {
            Image(systemName: "cursorarrow.rays")
                .font(.system(size: 9))
                .foregroundStyle(.secondary)

            if let date = cursorDate {
                Text(Self.cursorFormatter.string(from: date))
                    .foregroundStyle(.primary)
                    .padding(.trailing, 4)
            }

            ForEach(resolvedChannels) { desc in
                if let v = cursorValues[desc.id] {
                    HStack(spacing: 2) {
                        Circle().fill(desc.color).frame(width: 5, height: 5)
                        Text(String(format: desc.format, v)).foregroundStyle(desc.color)
                        Text(desc.unit).foregroundStyle(.secondary)
                    }
                }
            }

            Spacer()

            Button(action: clearCursor) {
                Image(systemName: "xmark.circle.fill")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
        }
        .font(.system(size: 10, design: .monospaced))
        .padding(.horizontal, 8).padding(.vertical, 5)
        .background(Color(.tertiarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 6))
    }

    @ViewBuilder private var footerRow: some View {
        HStack {
            if isPaused {
                Button(action: goLive) {
                    Label("Live", systemImage: "play.circle.fill")
                }
                .buttonStyle(.bordered).tint(.green)
            } else {
                Button(action: pause) {
                    Label("Pause", systemImage: "pause.circle")
                }
                .buttonStyle(.bordered).tint(.secondary)
            }

            Spacer()

            Text("\(Int(graph.windowSeconds))s")
                .font(.system(size: 9, design: .monospaced))
                .foregroundStyle(.tertiary)
        }
        .font(.system(size: 11, design: .monospaced))
    }

    private func valueBadge(_ desc: ChannelDescriptor) -> some View {
        HStack(spacing: 3) {
            Circle().fill(desc.color).frame(width: 7, height: 7)
            Group {
                if let v = logger.current[desc.id] {
                    Text(String(format: desc.format, v)).foregroundStyle(desc.color)
                } else {
                    Text("—").foregroundStyle(.tertiary)
                }
            }
            .font(.system(size: 10, weight: .medium, design: .monospaced))
        }
    }

    // MARK: Actions

    private func pause()  { frozenAt = Date(); clearCursor() }
    private func goLive() { frozenAt = nil;   clearCursor() }

    private func clearCursor() {
        cursorDate   = nil
        cursorValues = [:]
    }

    private func handleDrag(to date: Date) {
        if frozenAt == nil { frozenAt = Date() }
        cursorDate = date
        for id in graph.channels {
            guard let pts = logger.history[id], !pts.isEmpty else { continue }
            var lo = 0, hi = pts.count - 1
            while lo < hi {
                let mid = lo + (hi - lo) / 2
                if pts[mid].timestamp < date { lo = mid + 1 } else { hi = mid }
            }
            if lo > 0,
               abs(pts[lo - 1].timestamp.timeIntervalSince(date))
               < abs(pts[lo].timestamp.timeIntervalSince(date)) { lo -= 1 }
            cursorValues[id] = pts[lo].value
        }
    }
}

// MARK: - Braun gauge panel

private struct BraunGaugePanel: View {
    @Binding var graph: GraphConfig
    @ObservedObject var logger: DataLogger
    let onEdit:   () -> Void
    let onDelete: () -> Void

    private var channels: [ChannelDescriptor] {
        graph.channels.compactMap { logger.descriptor(for: $0) }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Text(graph.title)
                    .font(.system(.subheadline, design: .monospaced).bold())
                Spacer()
                Button(action: onEdit) {
                    Image(systemName: "slider.horizontal.3").font(.caption)
                }
                .tint(.secondary)
            }

            if channels.isEmpty {
                Text("No channel selected")
                    .font(.system(.subheadline, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 40)
            } else {
                HStack(spacing: 8) {
                    ForEach(channels) { ch in
                        BraunGaugeView(channel: ch, value: logger.current[ch.id])
                    }
                }
            }
        }
        .padding(12)
        .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 12))
    }
}

// MARK: - Braun gauge face
//
// Analog instrument dial inspired by Dieter Rams–era Braun clocks.
// Arc sweeps 270° from 7:30 (lower-left) clockwise to 4:30 (lower-right),
// leaving a 90° gap at 6 o'clock where the value readout lives.

private struct BraunGaugeView: View {
    let channel: ChannelDescriptor
    let value:   Double?

    // Arc geometry (angles in SwiftUI Canvas convention: 0° = east, clockwise)
    private static let startDeg: Double = 135   // 7:30 position (lower-left)
    private static let sweepDeg: Double = 270   // clockwise to 4:30 (lower-right)
    private static let majors:   Int    = 5     // labeled ticks: 0/25/50/75/100 %
    private static let minors:   Int    = 3     // minor ticks between each major pair

    private var fraction: Double {
        guard let v = value else { return 0 }
        return max(0, min(1, channel.normalized(v) / 100.0))
    }

    var body: some View {
        Canvas { ctx, size in
            let r = min(size.width, size.height) / 2 - 8
            let c = CGPoint(x: size.width / 2, y: size.height / 2)

            // ── Face circle ──────────────────────────────────────────────────
            var face = Path()
            face.addEllipse(in: CGRect(x: c.x - r, y: c.y - r,
                                        width: r * 2, height: r * 2))
            ctx.fill(face, with: .color(Color(.secondarySystemGroupedBackground)))
            ctx.stroke(face, with: .color(.primary.opacity(0.12)),
                       style: StrokeStyle(lineWidth: 1))

            // ── Tick marks and labels ────────────────────────────────────────
            let intervals = (Self.majors - 1) * (Self.minors + 1)   // = 16
            for i in 0...intervals {
                let pct    = Double(i) / Double(intervals)
                let isMajor = i % (Self.minors + 1) == 0
                let deg    = Self.startDeg + pct * Self.sweepDeg
                let rad    = deg * .pi / 180
                let cosA   = CGFloat(cos(rad))
                let sinA   = CGFloat(sin(rad))
                let outerR = r - 2
                let tLen: CGFloat = isMajor ? r * 0.14 : r * 0.07
                let tW:   CGFloat = isMajor ? 1.5 : 0.8

                var tick = Path()
                tick.move(to:    CGPoint(x: c.x + outerR * cosA,          y: c.y + outerR * sinA))
                tick.addLine(to: CGPoint(x: c.x + (outerR - tLen) * cosA, y: c.y + (outerR - tLen) * sinA))
                ctx.stroke(tick, with: .color(.primary.opacity(0.70)),
                           style: StrokeStyle(lineWidth: tW, lineCap: .butt))

                if isMajor {
                    let mi  = i / (Self.minors + 1)   // 0…4
                    let val = channel.yRange.lowerBound
                            + Double(mi) / Double(Self.majors - 1)
                            * (channel.yRange.upperBound - channel.yRange.lowerBound)
                    let lStr: String
                    if abs(val) >= 1000 { lStr = String(format: "%.0fk", val / 1000) }
                    else if val.truncatingRemainder(dividingBy: 1) == 0 { lStr = String(format: "%.0f", val) }
                    else { lStr = String(format: "%.1f", val) }

                    let lR  = outerR - tLen - r * 0.13
                    let lPt = CGPoint(x: c.x + lR * cosA, y: c.y + lR * sinA)
                    let lbl = ctx.resolve(Text(lStr)
                        .font(.system(size: r * 0.10, weight: .light))
                        .foregroundStyle(Color.primary.opacity(0.55)))
                    ctx.draw(lbl, at: lPt)
                }
            }

            // ── Needle ───────────────────────────────────────────────────────
            if value != nil {
                let nDeg  = Self.startDeg + fraction * Self.sweepDeg
                let nRad  = nDeg * .pi / 180
                let cosN  = CGFloat(cos(nRad))
                let sinN  = CGFloat(sin(nRad))
                let nLen  = r * 0.70
                let tail  = r * 0.12

                // Dark shaft (root → 80 % of needle length)
                var shaft = Path()
                shaft.move(to: CGPoint(x: c.x - tail * cosN,          y: c.y - tail * sinN))
                shaft.addLine(to: CGPoint(x: c.x + nLen * 0.80 * cosN, y: c.y + nLen * 0.80 * sinN))
                ctx.stroke(shaft, with: .color(.primary.opacity(0.88)),
                           style: StrokeStyle(lineWidth: 1.5, lineCap: .round))

                // Red tip (last 20 % of needle) — Braun second-hand accent
                var tipPath = Path()
                tipPath.move(to:    CGPoint(x: c.x + nLen * 0.80 * cosN, y: c.y + nLen * 0.80 * sinN))
                tipPath.addLine(to: CGPoint(x: c.x + nLen * cosN,         y: c.y + nLen * sinN))
                ctx.stroke(tipPath, with: .color(.red),
                           style: StrokeStyle(lineWidth: 2.2, lineCap: .round))
            }

            // ── Center pivot cap ─────────────────────────────────────────────
            let capR = r * 0.055
            var cap  = Path()
            cap.addEllipse(in: CGRect(x: c.x - capR, y: c.y - capR,
                                       width: capR * 2, height: capR * 2))
            ctx.fill(cap, with: .color(.primary))
            let dotR = capR * 0.42
            var dot  = Path()
            dot.addEllipse(in: CGRect(x: c.x - dotR, y: c.y - dotR,
                                       width: dotR * 2, height: dotR * 2))
            ctx.fill(dot, with: .color(Color(.secondarySystemGroupedBackground)))

            // ── Value readout in the lower-center gap (6 o'clock area) ───────
            let gapCenterY = c.y + r * 0.48
            let chLbl = ctx.resolve(Text(channel.label)
                .font(.system(size: r * 0.09, weight: .medium))
                .foregroundStyle(Color.secondary))
            ctx.draw(chLbl, at: CGPoint(x: c.x, y: gapCenterY - r * 0.16))

            if let v = value {
                let valLbl = ctx.resolve(Text(String(format: channel.format, v))
                    .font(.system(size: r * 0.20, weight: .light))
                    .foregroundStyle(Color.primary))
                ctx.draw(valLbl, at: CGPoint(x: c.x, y: gapCenterY + r * 0.06))

                let unitLbl = ctx.resolve(Text(channel.unit)
                    .font(.system(size: r * 0.09, weight: .regular))
                    .foregroundStyle(.secondary))
                ctx.draw(unitLbl, at: CGPoint(x: c.x, y: gapCenterY + r * 0.25))
            } else {
                let dash = ctx.resolve(Text("—")
                    .font(.system(size: r * 0.20, weight: .light))
                    .foregroundStyle(.tertiary))
                ctx.draw(dash, at: CGPoint(x: c.x, y: gapCenterY + r * 0.06))
            }
        }
        .aspectRatio(1, contentMode: .fit)
    }
}

// MARK: - Chart

private struct ChannelChart: View {
    let channels:   [ChannelDescriptor]
    let history:    [String: [DataPoint]]
    let windowSecs: Double
    let now:        Date
    let cursorDate: Date?
    let onDrag:     (Date) -> Void
    let onDragEnd:  () -> Void

    var body: some View {
        let windowStart = now.addingTimeInterval(-windowSecs)

        HStack(spacing: 0) {
            if let ch = channels.first {
                YAxisColumn(channel: ch, anchor: .trailing).frame(width: 32)
            }

            Chart {
                ForEach(channels) { ch in
                    ForEach(windowedPoints(history[ch.id], from: windowStart)) { pt in
                        LineMark(
                            x: .value("Time",  pt.timestamp),
                            y: .value("Value", pt.normalized),
                            series: .value("Ch", ch.id)
                        )
                        .interpolationMethod(.linear)
                    }
                    .foregroundStyle(ch.color)
                    .lineStyle(StrokeStyle(lineWidth: 1.5))
                }

                if let cd = cursorDate, cd >= windowStart {
                    RuleMark(x: .value("Cursor", cd))
                        .foregroundStyle(Color.primary.opacity(0.4))
                        .lineStyle(StrokeStyle(lineWidth: 1, dash: [3, 3]))
                        .zIndex(1)
                }
            }
            .chartXScale(domain: windowStart...now)
            .chartYScale(domain: 0...100)
            .chartXAxis {
                AxisMarks(values: .stride(by: max(windowSecs / 5, 1))) { value in
                    AxisGridLine(stroke: StrokeStyle(lineWidth: 0.5))
                        .foregroundStyle(Color.primary.opacity(0.15))
                    AxisValueLabel {
                        if let date = value.as(Date.self) {
                            Text(date, format: .dateTime
                                .hour(.twoDigits(amPM: .omitted))
                                .minute()
                                .second())
                                .font(.system(size: 9, design: .monospaced))
                                .foregroundStyle(.secondary)
                        }
                    }
                }
            }
            .chartYAxis {
                AxisMarks(values: [0, 50, 100]) {
                    AxisGridLine(stroke: StrokeStyle(lineWidth: 0.5))
                        .foregroundStyle(Color.primary.opacity(0.10))
                }
            }
            .chartLegend(.hidden)
            .chartOverlay { proxy in
                GeometryReader { geo in
                    Rectangle()
                        .fill(.clear)
                        .contentShape(Rectangle())
                        .gesture(
                            DragGesture(minimumDistance: 0)
                                .onChanged { val in
                                    let plotFrame = geo[proxy.plotAreaFrame]
                                    let xInPlot   = val.location.x - plotFrame.origin.x
                                    guard xInPlot >= 0, xInPlot <= plotFrame.width else { return }
                                    let date: Date? = proxy.value(atX: xInPlot)
                                    if let date {
                                        onDrag(max(windowStart, min(now, date)))
                                    }
                                }
                                .onEnded { _ in onDragEnd() }
                        )
                }
            }

            if channels.count >= 2 {
                YAxisColumn(channel: channels[1], anchor: .leading).frame(width: 32)
            }
        }
    }

    private func windowedPoints(_ pts: [DataPoint]?, from start: Date) -> ArraySlice<DataPoint> {
        guard let pts, !pts.isEmpty else { return [][...] }
        var lo = pts.startIndex, hi = pts.endIndex
        while lo < hi {
            let mid = lo + (hi - lo) / 2
            if pts[mid].timestamp < start { lo = mid + 1 } else { hi = mid }
        }
        return pts[lo...]
    }
}

// MARK: - Y-axis column

private struct YAxisColumn: View {
    let channel: ChannelDescriptor
    let anchor:  HorizontalAlignment

    var body: some View {
        VStack(alignment: anchor, spacing: 0) {
            Text(tick(channel.yRange.upperBound))
            Spacer()
            Text(tick(mid)).opacity(0.55)
            Spacer()
            Text(tick(channel.yRange.lowerBound))
        }
        .font(.system(size: 7, weight: .medium, design: .monospaced))
        .foregroundStyle(channel.color.opacity(0.9))
        .padding(.horizontal, 2)
    }

    private var mid: Double {
        (channel.yRange.upperBound + channel.yRange.lowerBound) / 2
    }

    private func tick(_ val: Double) -> String {
        abs(val) >= 1000 ? String(format: "%.0fk", val / 1000)
                         : String(format: "%.0f", val)
    }
}

// MARK: - Graph editor sheet

struct GraphEditorSheet: View {
    @Binding var graph: GraphConfig
    let available: [ChannelDescriptor]   // from logger.knownChannels
    let onDelete: () -> Void
    @Environment(\.dismiss) private var dismiss

    private let windows: [Double] = [30, 60, 120, 300, 600, 1800, 3600]

    var body: some View {
        NavigationStack {
            List {
                Section("Type") {
                    Picker("Type", selection: $graph.graphType) {
                        Text("Line").tag(GraphType.line)
                        Text("Gauge").tag(GraphType.gauge)
                    }
                    .pickerStyle(.segmented)
                }

                Section("Title") {
                    TextField("Graph title", text: $graph.title)
                }

                Section("Channels") {
                    ForEach(available) { desc in
                        Toggle(isOn: Binding(
                            get: { graph.channels.contains(desc.id) },
                            set: { on in
                                if on {
                                    if !graph.channels.contains(desc.id) {
                                        graph.channels.append(desc.id)
                                    }
                                } else {
                                    graph.channels.removeAll { $0 == desc.id }
                                }
                            }
                        )) {
                            HStack(spacing: 8) {
                                Circle().fill(desc.color).frame(width: 10, height: 10)
                                Text("\(desc.label)  (\(desc.unit))")
                                    .font(.system(.body, design: .monospaced))
                            }
                        }
                    }
                }

                if graph.graphType == .line {
                    Section("Time Window") {
                        Picker("Window", selection: $graph.windowSeconds) {
                            ForEach(windows, id: \.self) { w in
                                Text(windowLabel(w)).tag(w)
                            }
                        }
                        .pickerStyle(.segmented)
                    }
                }

                Section {
                    Button("Delete Graph", role: .destructive, action: onDelete)
                }
            }
            .navigationTitle("Edit Graph")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }

    private func windowLabel(_ w: Double) -> String {
        if w < 60  { return "\(Int(w))s" }
        if w < 3600 { return "\(Int(w / 60))m" }
        return "\(Int(w / 3600))h"
    }
}

// MARK: - Session stats

private struct SessionStatsView: View {
    @ObservedObject var logger: DataLogger

    private var activeChannels: [ChannelDescriptor] {
        logger.sessionMin.keys
            .compactMap { logger.descriptor(for: $0) }
            .sorted { $0.label < $1.label }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Session Stats")
                .font(.system(.subheadline, design: .monospaced).bold())
                .foregroundStyle(.secondary)

            ForEach(activeChannels) { desc in
                HStack(spacing: 4) {
                    Circle().fill(desc.color).frame(width: 7, height: 7)
                    Text(desc.label)
                        .font(.system(size: 11, design: .monospaced))
                        .frame(width: 56, alignment: .leading)
                    Spacer()
                    if let v = logger.sessionMin[desc.id] { statCell("min", desc.format, v, .blue) }
                    if let v = logger.sessionMax[desc.id] { statCell("max", desc.format, v, .red) }
                    if let v = logger.sessionAvg[desc.id] { statCell("avg", desc.format, v, .green) }
                }
            }
        }
        .padding(12)
        .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 12))
    }

    private func statCell(_ label: String, _ fmt: String, _ val: Double, _ color: Color) -> some View {
        VStack(spacing: 1) {
            Text(label)
                .font(.system(size: 7, design: .monospaced))
                .foregroundStyle(.tertiary)
            Text(String(format: fmt, val))
                .font(.system(size: 11, weight: .medium, design: .monospaced))
                .foregroundStyle(color)
        }
        .frame(width: 50)
    }
}

// MARK: - Share sheet (iOS)

#if canImport(UIKit)
struct ShareSheet: UIViewControllerRepresentable {
    let url: URL

    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: [url], applicationActivities: nil)
    }

    func updateUIViewController(_ vc: UIActivityViewController, context: Context) {}
}
#endif
