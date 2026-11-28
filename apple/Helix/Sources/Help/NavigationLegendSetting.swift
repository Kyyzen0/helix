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

/// What the mouse, the trackpad and the keyboard do in the viewport, for each preset of Settings >
/// Navigation. It follows `NavigationMapper`, which does it: when one changes the other does, and the
/// tests hold the two together.
enum NavigationBindings {
    struct Gesture: Identifiable, Equatable {
        let gesture: String
        let action: String
        var id: String { gesture }
    }

    /// The gestures of a preset, in the language of the app. Read when the legend is drawn, so a
    /// change of language or of preset shows at once.
    static func rows(for preset: NavigationPreset) -> [Gesture] {
        let frame = Gesture(gesture: String(localized: "F"), action: String(localized: "Frame the geometry"))
        let orbit = String(localized: "Orbit"), pan = String(localized: "Pan"), zoom = String(localized: "Zoom")
        switch preset {
        case .trackpad:
            return [
                Gesture(gesture: String(localized: "Drag"), action: orbit),
                Gesture(gesture: String(localized: "⌥ drag, or middle button"), action: pan),
                Gesture(gesture: String(localized: "⌃ drag, right button, or wheel"), action: zoom),
                Gesture(gesture: String(localized: "Two-finger scroll"), action: orbit),
                Gesture(gesture: String(localized: "⇧ two-finger scroll"), action: pan),
                Gesture(gesture: String(localized: "⌘ two-finger scroll, or pinch"), action: zoom),
                frame,
            ]
        case .mouse:
            return [
                Gesture(gesture: String(localized: "Drag"), action: orbit),
                Gesture(gesture: String(localized: "⌥ or ⇧ drag, or middle button"), action: pan),
                Gesture(gesture: String(localized: "⌃ drag, or right button"), action: zoom),
                Gesture(gesture: String(localized: "Scroll, or pinch"), action: zoom),
                Gesture(gesture: String(localized: "⇧ scroll"), action: pan),
                Gesture(gesture: String(localized: "⌥ scroll"), action: orbit),
                frame,
            ]
        case .blender:
            return [
                Gesture(gesture: String(localized: "Middle button drag, or drag"), action: orbit),
                Gesture(gesture: String(localized: "⇧ middle button drag, or ⌥ drag"), action: pan),
                Gesture(gesture: String(localized: "⌃ middle button drag"), action: zoom),
                Gesture(gesture: String(localized: "Wheel, or pinch"), action: zoom),
                Gesture(gesture: String(localized: "⇧ wheel"), action: pan),
                Gesture(gesture: String(localized: "⌥ wheel"), action: orbit),
                frame,
            ]
        }
    }
}
