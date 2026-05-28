import SwiftUI

// MARK: - Acapulco Blue

extension Color {
    /// #006DAC — brand primary; prominent in both light and dark mode.
    static let acapulcoBlue = Color(red: 0.0, green: 109.0 / 255.0, blue: 172.0 / 255.0)
}

// MARK: - Font-scale settings

/// Persisted per-category font-scale multipliers. Stored in UserDefaults via @AppStorage.
final class AppSettings: ObservableObject {
    @AppStorage("fs.gauge")   var gaugeScale:   Double = 1.0   // big dial / gauge values
    @AppStorage("fs.label")   var labelScale:   Double = 1.0   // panel headers, channel labels
    @AppStorage("fs.data")    var dataScale:    Double = 1.0   // CAN frames, live readouts
    @AppStorage("fs.axis")    var axisScale:    Double = 1.0   // chart axis tick labels
    @AppStorage("fs.caption") var captionScale: Double = 1.0   // units, secondary captions
}

extension AppSettings {
    // Base sizes (pt) — intentionally larger than the original hard-coded values.
    static let gaugeBase:   CGFloat = 20
    static let labelBase:   CGFloat = 14
    static let dataBase:    CGFloat = 13
    static let axisBase:    CGFloat = 9
    static let captionBase: CGFloat = 10

    func gaugePt()   -> CGFloat { AppSettings.gaugeBase   * CGFloat(gaugeScale)   }
    func labelPt()   -> CGFloat { AppSettings.labelBase   * CGFloat(labelScale)   }
    func dataPt()    -> CGFloat { AppSettings.dataBase    * CGFloat(dataScale)    }
    func axisPt()    -> CGFloat { AppSettings.axisBase    * CGFloat(axisScale)    }
    func captionPt() -> CGFloat { AppSettings.captionBase * CGFloat(captionScale) }

    func gaugeFont()   -> Font { .system(size: gaugePt(),   weight: .semibold, design: .monospaced) }
    func labelFont()   -> Font { .system(size: labelPt(),   weight: .medium,   design: .monospaced) }
    func dataFont()    -> Font { .system(size: dataPt(),                       design: .monospaced) }
    func axisFont()    -> Font { .system(size: axisPt(),    weight: .medium,   design: .monospaced) }
    func captionFont() -> Font { .system(size: captionPt(),                    design: .monospaced) }
}

// MARK: - Appearance settings sheet

struct AppearanceSheet: View {
    @EnvironmentObject private var settings: AppSettings
    @Environment(\.dismiss)  private var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section("Font Sizes") {
                    ScaleRow(
                        title:   "Gauge Values",
                        example: "4523   14.7",
                        base:    AppSettings.gaugeBase,
                        scale:   $settings.gaugeScale
                    )
                    ScaleRow(
                        title:   "Panel Labels",
                        example: "RPM   Speed   AFR",
                        base:    AppSettings.labelBase,
                        scale:   $settings.labelScale
                    )
                    ScaleRow(
                        title:   "Data Stream",
                        example: "0x100 [2]  01 FF",
                        base:    AppSettings.dataBase,
                        scale:   $settings.dataScale
                    )
                    ScaleRow(
                        title:   "Chart Axis",
                        example: "12:34:56   0   50   100",
                        base:    AppSettings.axisBase,
                        scale:   $settings.axisScale
                    )
                    ScaleRow(
                        title:   "Captions",
                        example: "rpm   °F   kPa   %",
                        base:    AppSettings.captionBase,
                        scale:   $settings.captionScale
                    )
                }

                Section {
                    Button("Reset to Defaults", role: .destructive) {
                        withAnimation(.easeInOut(duration: 0.2)) {
                            settings.gaugeScale   = 1.0
                            settings.labelScale   = 1.0
                            settings.dataScale    = 1.0
                            settings.axisScale    = 1.0
                            settings.captionScale = 1.0
                        }
                    }
                }
            }
            .navigationTitle("Appearance")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button("Done") { dismiss() }
                        .tint(.acapulcoBlue)
                }
            }
        }
    }
}

private struct ScaleRow: View {
    let title:   String
    let example: String
    let base:    CGFloat
    @Binding var scale: Double

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(title)
                    .font(.subheadline.bold())
                Spacer()
                Text(String(format: "%.1f×", scale))
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .frame(width: 38, alignment: .trailing)
            }
            Text(example)
                .font(.system(size: base * CGFloat(scale), design: .monospaced))
                .foregroundStyle(Color.acapulcoBlue)
                .lineLimit(1)
                .minimumScaleFactor(0.5)
            Slider(value: $scale, in: 0.7...1.8, step: 0.1)
                .tint(.acapulcoBlue)
        }
        .padding(.vertical, 4)
    }
}
