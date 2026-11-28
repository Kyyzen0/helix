import SwiftUI

/// The Navigation tab as the Settings window shows it: the choices are the ones kept in the user
/// defaults, which `HelixMetalView` reads at every event, so a change applies at once.
struct NavigationSettingsTab: View {
    @AppStorage(NavigationPreset.defaultsKey) private var preset = NavigationPreset.default.rawValue
    @AppStorage(NavigationOptions.invertZoomKey) private var invertZoom = false
    @AppStorage(NavigationOptions.orbitSpeedKey) private var orbitSpeed = 1.0
    @AppStorage(NavigationOptions.zoomSpeedKey) private var zoomSpeed = 1.0
    @AppStorage(NavigationOptions.ignoreInertiaKey) private var ignoreInertia = true

    var body: some View {
        NavigationPane(
            preset: Binding(get: { NavigationPreset(rawValue: preset) ?? .default }, set: { preset = $0.rawValue }),
            invertZoom: $invertZoom, orbitSpeed: $orbitSpeed, zoomSpeed: $zoomSpeed, ignoreInertia: $ignoreInertia
        )
    }
}

/// The preset, what it does, and the options on top of it.
struct NavigationPane: View {
    @Binding var preset: NavigationPreset
    @Binding var invertZoom: Bool
    @Binding var orbitSpeed: Double
    @Binding var zoomSpeed: Double
    @Binding var ignoreInertia: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Navigation").font(.headline)
            Picker("Navigation", selection: $preset) {
                ForEach(NavigationPreset.allCases) { choice in
                    Text(choice.title).tag(choice)
                }
            }
            .labelsHidden()
            .pickerStyle(.segmented)
            Text(preset.summary)
                .font(.callout)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Grid(alignment: .leading, horizontalSpacing: 16, verticalSpacing: 3) {
                ForEach(NavigationBindings.rows(for: preset)) { row in
                    GridRow {
                        Text(verbatim: row.gesture).foregroundStyle(.secondary)
                        Text(verbatim: row.action)
                    }
                    .font(.callout)
                }
            }
            Divider()
            Toggle("Invert zoom direction", isOn: $invertZoom)
            SpeedSlider(title: String(localized: "Orbit speed"), value: $orbitSpeed)
            SpeedSlider(title: String(localized: "Zoom speed"), value: $zoomSpeed)
            Toggle("Ignore scroll inertia for orbit", isOn: $ignoreInertia)
            Text("Stops the scene from turning on after your fingers leave the trackpad or the mouse. Zooming and panning stay smooth.")
                .font(.callout)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(20)
        .frame(maxWidth: .infinity, alignment: .leading)
        .fixedSize(horizontal: false, vertical: true)
    }
}

/// A multiplier from a quarter to four, on a scale where each step doubles it, so that 1 sits in
/// the middle and the slowest and the fastest are as far from it.
struct SpeedSlider: View {
    let title: String
    @Binding var value: Double

    private var range: ClosedRange<Double> {
        Double(NavigationOptions.speedRange.lowerBound)...Double(NavigationOptions.speedRange.upperBound)
    }

    var body: some View {
        HStack {
            Text(verbatim: title).lineLimit(1).frame(width: 150, alignment: .leading)
            Slider(
                value: Binding(
                    get: { log2(Double(NavigationOptions.clampedSpeed(value))) },
                    set: { value = Double(NavigationOptions.clampedSpeed(pow(2, $0))) }
                ),
                in: log2(range.lowerBound)...log2(range.upperBound)
            )
            Text(verbatim: String(format: "%.2g×", Double(NavigationOptions.clampedSpeed(value))))
                .monospacedDigit()
                .frame(width: 44, alignment: .trailing)
        }
    }
}
