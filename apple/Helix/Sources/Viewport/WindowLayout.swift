import Foundation

/// How a window shares its room: three presets for what a person is doing.
///
/// A preset only sets the settings the window already keeps (the share of the viewport, the
/// inspector, the timeline, the bottom pane), so the person can still drag the divider or toggle a
/// column afterwards, and the window follows at once since it reads the same defaults. Only opening
/// a 3D file picks one on its own (Viewing); nothing else changes the layout by itself.
enum WindowLayout: String, CaseIterable, Identifiable, Sendable {
    /// The viewport large, and nothing beside it.
    case viewing
    /// The viewport over the network, with the inspector: the layout the window had always.
    case building
    /// The timeline shown, and the viewport taking more room than the network.
    case animating

    /// The key of the share of the window the viewport takes, which `ViewportSplit` reads.
    static let fractionKey = "viewportFraction"

    var id: String { rawValue }

    /// The settings the preset writes, by key.
    var settings: [String: Any] {
        switch self {
        case .viewing:
            [Self.fractionKey: 0.8, InspectorSetting.defaultsKey: false]
        case .building:
            [
                Self.fractionKey: 0.6, InspectorSetting.defaultsKey: true,
                TimelineSetting.lowerPaneKey: TimelineSetting.LowerPane.network.rawValue,
            ]
        case .animating:
            [
                Self.fractionKey: 0.7, InspectorSetting.defaultsKey: true, TimelineSetting.modeKey: TimelineMode.always.rawValue,
                TimelineSetting.lowerPaneKey: TimelineSetting.LowerPane.network.rawValue,
            ]
        }
    }

    func apply(to defaults: UserDefaults = .standard) {
        for (key, value) in settings { defaults.set(value, forKey: key) }
    }
}
