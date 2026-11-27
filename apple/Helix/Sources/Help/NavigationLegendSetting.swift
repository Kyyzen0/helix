import Foundation

/// Whether the legend of the navigation is shown over the viewport. The person hides it with its
/// close button, and Help > Show Navigation Help brings it back. Kept in the user defaults, so
/// every window shares it and it survives a restart.
enum NavigationLegendSetting {
    static let hiddenKey = "navigationLegendHidden"

    static func isHidden(in defaults: UserDefaults = .standard) -> Bool {
        defaults.bool(forKey: hiddenKey)
    }

    static func setHidden(_ hidden: Bool, in defaults: UserDefaults = .standard) {
        defaults.set(hidden, forKey: hiddenKey)
    }
}

/// What the mouse, the trackpad and the keyboard do in the viewport. It follows `HelixMetalView`,
/// which does it: when one changes the other does.
enum NavigationBindings {
    struct Gesture: Identifiable, Equatable {
        let gesture: String
        let action: String
        var id: String { gesture }
    }

    /// The gestures, in the language of the app. Read when the legend is drawn, so a change of
    /// language shows at once.
    static var rows: [Gesture] {
        [
            Gesture(gesture: String(localized: "Drag"), action: String(localized: "Orbit")),
            Gesture(gesture: String(localized: "⌥ drag, or middle button"), action: String(localized: "Pan")),
            Gesture(gesture: String(localized: "⌃ drag, right button, or wheel"), action: String(localized: "Zoom")),
            Gesture(gesture: String(localized: "Two-finger scroll"), action: String(localized: "Orbit")),
            Gesture(gesture: String(localized: "⇧ two-finger scroll"), action: String(localized: "Pan")),
            Gesture(gesture: String(localized: "⌘ two-finger scroll, or pinch"), action: String(localized: "Zoom")),
            Gesture(gesture: String(localized: "F"), action: String(localized: "Frame the geometry")),
        ]
    }
}
