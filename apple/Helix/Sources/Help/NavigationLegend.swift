import SwiftUI

/// A small card in the corner of the viewport that says how to move around in it.
struct NavigationLegend: View {
    @Environment(\.theme) private var theme
    @Environment(\.interfaceSize) private var size
    let onHide: () -> Void
    @AppStorage(NavigationPreset.defaultsKey) private var presetName = NavigationPreset.default.rawValue
    @Environment(\.openSettings) private var openSettings
    @AppStorage(SettingsTab.defaultsKey) private var settingsTab = SettingsTab.general.rawValue

    private var preset: NavigationPreset { NavigationPreset(rawValue: presetName) ?? .default }

    var body: some View {
        VStack(alignment: .leading, spacing: size.points(2)) {
            HStack(alignment: .firstTextBaseline) {
                Text("Navigation")
                    .uiFont(.caption, weight: .semibold)
                Text(verbatim: preset.title)
                    .uiFont(.caption)
                    .foregroundStyle(.secondary)
                Spacer(minLength: size.points(12))
                Button(action: onHide) {
                    Image(systemName: "xmark")
                        .font(.system(size: size.points(9), weight: .bold))
                        .contentShape(Rectangle())
                }
                .buttonStyle(.borderless)
                .help("Hide this help. Help > Show Navigation Help brings it back.")
                .accessibilityLabel(Text("Hide navigation help"))
            }
            ForEach(NavigationBindings.rows(for: preset)) { row in
                HStack(alignment: .firstTextBaseline, spacing: size.points(8)) {
                    Text(verbatim: row.gesture)
                        .uiFont(.caption)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .fixedSize()
                        .frame(minWidth: size.points(190), alignment: .leading)
                    Text(verbatim: row.action)
                        .uiFont(.caption)
                        .lineLimit(1)
                        .fixedSize()
                }
            }
            // Settings opens on the tab that was last in front, so it is set to Navigation first.
            Button {
                settingsTab = SettingsTab.navigation.rawValue
                openSettings()
            } label: {
                Text("Change…")
                    .uiFont(.caption)
            }
            .buttonStyle(.link)
            .help("Open Settings > Navigation to choose the preset, the speed and the direction of the zoom.")
        }
        .padding(size.points(9))
        .fixedSize()
        .background(
            RoundedRectangle(cornerRadius: 8)
                .fill(.regularMaterial)
        )
        .overlay(RoundedRectangle(cornerRadius: 8).stroke(Color.primary.opacity(0.12)))
        .accessibilityElement(children: .contain)
        .accessibilityLabel(Text("Navigation"))
    }
}
