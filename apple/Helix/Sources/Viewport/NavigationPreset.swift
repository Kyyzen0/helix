import Foundation

/// How a pointing device moves the camera: three presets, kept in the user defaults like the theme,
/// so every window shares the choice and it takes effect at the next event.
///
/// Trackpad is how the viewport always worked: a two finger scroll orbits. Mouse is for a mouse and
/// for a Magic Mouse, whose scroll arrives as precise deltas just like a trackpad's and so used to
/// orbit; here a scroll zooms. Blender follows the middle button of Blender, and lets the left button
/// do the same so that a person without a middle button is not stuck.
enum NavigationPreset: String, CaseIterable, Identifiable, Sendable {
    case trackpad
    case mouse
    case blender

    static let defaultsKey = "navigationPreset"
    static let `default` = NavigationPreset.trackpad

    var id: String { rawValue }

    var title: String {
        switch self {
        case .trackpad: String(localized: "Trackpad")
        case .mouse: String(localized: "Mouse or Magic Mouse")
        case .blender: String(localized: "Blender")
        }
    }

    /// One sentence about who the preset is for.
    var summary: String {
        switch self {
        case .trackpad: String(localized: "Two-finger scroll orbits and pinch zooms. The way Helix has always worked.")
        case .mouse: String(localized: "Scrolling zooms, with a wheel or the surface of a Magic Mouse. Drag to orbit.")
        case .blender: String(localized: "The middle button orbits, as in Blender. The left button orbits too, so no middle button is needed.")
        }
    }

    static func stored(in defaults: UserDefaults = .standard) -> NavigationPreset {
        defaults.string(forKey: defaultsKey).flatMap(NavigationPreset.init(rawValue:)) ?? .default
    }
}

/// The choices of the Navigation tab that are not a preset.
struct NavigationOptions: Equatable, Sendable {
    /// Zooming the other way: scrolling or dragging that zoomed in now zooms out.
    var invertZoom = false
    /// A multiplier on the orbit amounts. 1 is the sensitivity the viewport always had.
    var orbitSpeed: Float = 1
    /// A multiplier on the zoom amounts, pinch included.
    var zoomSpeed: Float = 1
    /// Whether the scroll events that follow the lift of the fingers (the inertia of a trackpad
    /// or of a Magic Mouse) stop short of orbiting. They still zoom and pan, which is smooth.
    var ignoreScrollInertia = true

    static let speedRange: ClosedRange<Float> = 0.25...4

    static let invertZoomKey = "navigationInvertZoom"
    static let orbitSpeedKey = "navigationOrbitSpeed"
    static let zoomSpeedKey = "navigationZoomSpeed"
    static let ignoreInertiaKey = "navigationIgnoreScrollInertia"

    /// A speed read from the defaults: one that was edited by hand to something the slider cannot
    /// show is brought to the nearest end, and one that is not a number is 1.
    static func clampedSpeed(_ value: Double) -> Float {
        guard value.isFinite else { return 1 }
        return min(max(Float(value), speedRange.lowerBound), speedRange.upperBound)
    }

    static func stored(in defaults: UserDefaults = .standard) -> NavigationOptions {
        var options = NavigationOptions()
        options.invertZoom = defaults.bool(forKey: invertZoomKey)
        if defaults.object(forKey: orbitSpeedKey) != nil { options.orbitSpeed = clampedSpeed(defaults.double(forKey: orbitSpeedKey)) }
        if defaults.object(forKey: zoomSpeedKey) != nil { options.zoomSpeed = clampedSpeed(defaults.double(forKey: zoomSpeedKey)) }
        // On until the person turns it off; bool(forKey:) would read a missing key as off.
        if defaults.object(forKey: ignoreInertiaKey) != nil { options.ignoreScrollInertia = defaults.bool(forKey: ignoreInertiaKey) }
        return options
    }
}

/// The modifier keys held during an event, without AppKit so the mapping can be tested.
struct NavigationModifiers: OptionSet, Sendable {
    let rawValue: Int
    static let shift = NavigationModifiers(rawValue: 1)
    static let control = NavigationModifiers(rawValue: 2)
    static let option = NavigationModifiers(rawValue: 4)
    static let command = NavigationModifiers(rawValue: 8)
}

enum NavigationButton: Sendable {
    case left
    case right
    /// The middle button, and any button after it.
    case middle
}

/// A scroll event: a two finger scroll, a mouse wheel or the surface of a Magic Mouse.
struct NavigationScroll: Equatable, Sendable {
    var deltaX: Float
    var deltaY: Float
    /// Whether the deltas are points (a trackpad, a Magic Mouse) and not lines (a notched wheel).
    var precise: Bool
    /// Whether the event comes after the fingers lifted, from the inertia of the scroll.
    var momentum = false
    var modifiers: NavigationModifiers = []
}

/// A drag with a button held.
struct NavigationDrag: Equatable, Sendable {
    var deltaX: Float
    var deltaY: Float
    var button: NavigationButton
    var modifiers: NavigationModifiers = []
}

/// What an event does to the camera, in the units of the facade (`orbitViewport`, `panViewport`
/// and `dollyViewport`).
enum NavigationMove: Equatable, Sendable {
    case orbit(Float, Float)
    case pan(Float, Float)
    case dolly(Float)
}

/// The mapping from what the hands do to what the camera does. Pure, so every row of every preset
/// can be tested; `HelixMetalView` only turns an `NSEvent` into the input and applies the output.
enum NavigationMapper {
    /// Points one line of a notched wheel is worth when the wheel pans or orbits.
    private static let pointsPerLine: Float = 10

    static func scroll(_ scroll: NavigationScroll, preset: NavigationPreset, options: NavigationOptions) -> NavigationMove? {
        let unit = scroll.precise ? 1 : pointsPerLine
        let modifiers = scroll.modifiers
        let move: NavigationMove
        switch preset {
        case .trackpad:
            if !scroll.precise {
                // A notched wheel on the trackpad preset: it zooms, whatever key is held.
                move = zoom(scroll.deltaY * NavigationSettings.wheelZoomPerLine, options)
            } else if modifiers.contains(.command) {
                move = zoom(scroll.deltaY * NavigationSettings.scrollZoomPerPoint, options)
            } else if modifiers.contains(.shift) {
                move = pan(scroll.deltaX, scroll.deltaY, unit)
            } else {
                move = orbit(scroll.deltaX, scroll.deltaY, unit, options)
            }
        case .mouse, .blender:
            if modifiers.contains(.shift) {
                move = pan(scroll.deltaX, scroll.deltaY, unit)
            } else if modifiers.contains(.option) {
                move = orbit(scroll.deltaX, scroll.deltaY, unit, options)
            } else {
                let perUnit = scroll.precise ? NavigationSettings.scrollZoomPerPoint : NavigationSettings.wheelZoomPerLine
                move = zoom(scroll.deltaY * perUnit, options)
            }
        }
        // The inertia of a scroll that orbited would keep the model turning after the fingers lift.
        if scroll.momentum, options.ignoreScrollInertia, case .orbit = move { return nil }
        return nonZero(move)
    }

    static func drag(_ drag: NavigationDrag, preset: NavigationPreset, options: NavigationOptions) -> NavigationMove? {
        let modifiers = drag.modifiers
        let move: NavigationMove
        switch (preset, drag.button) {
        case (.blender, .right):
            return nil
        case (_, .right):
            move = dragZoom(drag, options)
        case (.blender, .middle):
            if modifiers.contains(.control) {
                move = dragZoom(drag, options)
            } else if modifiers.contains(.shift) {
                move = pan(drag.deltaX, drag.deltaY, 1)
            } else {
                move = orbit(drag.deltaX, drag.deltaY, 1, options)
            }
        case (_, .middle):
            move = pan(drag.deltaX, drag.deltaY, 1)
        case (.trackpad, .left):
            if modifiers.contains(.control) {
                move = dragZoom(drag, options)
            } else if modifiers.contains(.option) {
                move = pan(drag.deltaX, drag.deltaY, 1)
            } else {
                move = orbit(drag.deltaX, drag.deltaY, 1, options)
            }
        case (.mouse, .left):
            if modifiers.contains(.control) {
                move = dragZoom(drag, options)
            } else if modifiers.contains(.option) || modifiers.contains(.shift) {
                move = pan(drag.deltaX, drag.deltaY, 1)
            } else {
                move = orbit(drag.deltaX, drag.deltaY, 1, options)
            }
        case (.blender, .left):
            if modifiers.contains(.option) {
                move = pan(drag.deltaX, drag.deltaY, 1)
            } else {
                move = orbit(drag.deltaX, drag.deltaY, 1, options)
            }
        }
        return nonZero(move)
    }

    /// A pinch zooms in every preset. `magnification` is what `NSEvent.magnification` says.
    static func pinch(magnification: Float, options: NavigationOptions) -> NavigationMove? {
        nonZero(zoom(magnification * NavigationSettings.pinchZoom, options))
    }

    /// A two finger rotation turns the scene around the vertical axis, in every preset.
    static func rotation(degrees: Float, options: NavigationOptions) -> NavigationMove? {
        nonZero(.orbit(degrees * NavigationSettings.rotationPerDegree * options.orbitSpeed, 0))
    }

    // MARK: Pieces

    private static func orbit(_ deltaX: Float, _ deltaY: Float, _ unit: Float, _ options: NavigationOptions) -> NavigationMove {
        let scale = unit * NavigationSettings.orbitPerPoint * options.orbitSpeed
        return .orbit(deltaX * scale, deltaY * scale)
    }

    private static func pan(_ deltaX: Float, _ deltaY: Float, _ unit: Float) -> NavigationMove {
        let scale = unit * NavigationSettings.panPerPoint
        return .pan(deltaX * scale, deltaY * scale)
    }

    private static func dragZoom(_ drag: NavigationDrag, _ options: NavigationOptions) -> NavigationMove {
        zoom(drag.deltaY * NavigationSettings.dragZoomPerPoint, options)
    }

    /// The speed and the direction of a zoom, which are the same for a scroll, a drag and a pinch.
    private static func zoom(_ amount: Float, _ options: NavigationOptions) -> NavigationMove {
        .dolly(amount * options.zoomSpeed * (options.invertZoom ? -1 : 1))
    }

    /// A move that moves nothing is no move: no frame is asked for.
    private static func nonZero(_ move: NavigationMove) -> NavigationMove? {
        switch move {
        case .orbit(let x, let y), .pan(let x, let y): x == 0 && y == 0 ? nil : move
        case .dolly(let amount): amount == 0 ? nil : move
        }
    }
}
