// Every sensitivity of the viewport navigation, in one place.
//
// The facade moves the camera by "pixels of a pointer drag". A trackpad or a mouse gives points
// (and lines, for a notched wheel), so each constant says how many of those units one point of
// the gesture is worth. A larger number is a faster camera; a negative number reverses it.

enum NavigationSettings {
    /// Orbit units per point of a two finger scroll or a click drag.
    static let orbitPerPoint: Float = 1.0

    /// Pan units per point of a shift two finger scroll, an option drag or a middle button drag.
    static let panPerPoint: Float = 1.0

    /// Dolly amount per point of a command two finger scroll. Scrolling up zooms in.
    static let scrollZoomPerPoint: Float = -0.05

    /// Dolly amount per point of a control drag or a right button drag. Dragging down zooms out.
    static let dragZoomPerPoint: Float = 0.05

    /// Dolly amount per line of a notched mouse wheel. Rolling up zooms in.
    static let wheelZoomPerLine: Float = -0.6

    /// Dolly amount for a pinch that changes the magnification by 1, which is a full pinch out.
    /// Pinching out zooms in.
    static let pinchZoom: Float = -5.0

    /// Orbit units per degree of a two finger rotation, which turns the scene around the
    /// vertical axis.
    static let rotationPerDegree: Float = 1.75

    // The three presets that share these sensitivities, and the speed options on top of them, are
    // in `NavigationMapper`.
}
