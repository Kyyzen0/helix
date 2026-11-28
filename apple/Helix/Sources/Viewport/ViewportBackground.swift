import Foundation

/// What the viewport is painted on: the colour of its theme, or a dark, a medium or a light grey.
///
/// A choice for the viewport alone, kept in the user defaults like the theme: every window shares
/// it. A light background suits a dark model, and a person working in a bright room.
enum ViewportBackground: String, CaseIterable, Identifiable, Sendable {
    /// The colours of the theme chosen in the Settings window.
    case theme
    case dark
    case medium
    case light

    static let defaultsKey = "viewportBackground"
    static let `default` = ViewportBackground.theme

    var id: String { rawValue }

    var title: String {
        switch self {
        case .theme: String(localized: "Theme")
        case .dark: String(localized: "Dark")
        case .medium: String(localized: "Medium")
        case .light: String(localized: "Light")
        }
    }

    static func stored(in defaults: UserDefaults = .standard) -> ViewportBackground {
        defaults.string(forKey: defaultsKey).flatMap(ViewportBackground.init(rawValue:)) ?? .default
    }

    /// The colours of the viewport on this background: what is behind the geometry, the colour of
    /// a face that has no material, and the lines of the grid. They are chosen together, so the
    /// grid and the shape stay readable on each.
    struct Colors: Equatable, Sendable {
        let background: RGB
        let geometry: RGB
        let grid: RGB

        /// Whether the overlays on the viewport (the cube, the legend, the messages) are drawn for a
        /// dark background, with light text: whichever of white and black reads better on it.
        var isDark: Bool {
            background.contrast(with: RGB(red: 1, green: 1, blue: 1)) >= background.contrast(with: RGB(red: 0, green: 0, blue: 0))
        }
    }

    func colors(for palette: ThemePalette) -> Colors {
        switch self {
        case .theme:
            // The grid of a theme was drawn to be quiet; lifted so its lines can be told from the
            // background.
            Colors(
                background: palette.viewportBackground,
                geometry: palette.viewportGeometry,
                grid: palette.viewportGrid.mixed(with: RGB(red: 1, green: 1, blue: 1), by: 0.5)
            )
        case .dark:
            Colors(
                background: RGB(red: 0.13, green: 0.14, blue: 0.16),
                geometry: RGB(red: 0.62, green: 0.64, blue: 0.68),
                grid: RGB(red: 0.52, green: 0.54, blue: 0.62)
            )
        case .medium:
            Colors(
                background: RGB(red: 0.40, green: 0.42, blue: 0.46),
                geometry: RGB(red: 0.80, green: 0.82, blue: 0.86),
                grid: RGB(red: 0.80, green: 0.82, blue: 0.88)
            )
        case .light:
            Colors(
                background: RGB(red: 0.86, green: 0.87, blue: 0.89),
                geometry: RGB(red: 0.40, green: 0.42, blue: 0.47),
                grid: RGB(red: 0.25, green: 0.27, blue: 0.33)
            )
        }
    }
}
