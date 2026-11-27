import Foundation

/// A colour in sRGB, each channel from 0 to 1. A plain value, so the palettes can be checked
/// (contrast, round trips) without a window.
struct RGB: Equatable, Sendable {
    var red: Double
    var green: Double
    var blue: Double

    init(red: Double, green: Double, blue: Double) {
        self.red = red
        self.green = green
        self.blue = blue
    }

    /// From a hexadecimal value, 0xRRGGBB.
    init(hex: UInt32) {
        self.init(
            red: Double((hex >> 16) & 0xFF) / 255,
            green: Double((hex >> 8) & 0xFF) / 255,
            blue: Double(hex & 0xFF) / 255
        )
    }

    /// The colour as "#RRGGBB".
    var hexString: String {
        func byte(_ value: Double) -> Int { Int((min(max(value, 0), 1) * 255).rounded()) }
        return String(format: "#%02X%02X%02X", byte(red), byte(green), byte(blue))
    }

    /// The relative luminance of WCAG 2: 0 for black, 1 for white.
    var luminance: Double {
        func linear(_ channel: Double) -> Double {
            channel <= 0.03928 ? channel / 12.92 : pow((channel + 0.055) / 1.055, 2.4)
        }
        return 0.2126 * linear(red) + 0.7152 * linear(green) + 0.0722 * linear(blue)
    }

    /// The contrast ratio of WCAG 2 between two colours, from 1 (the same) to 21 (black on white).
    func contrast(with other: RGB) -> Double {
        let first = luminance
        let second = other.luminance
        return (max(first, second) + 0.05) / (min(first, second) + 0.05)
    }

    /// The hue in degrees, from 0 to 360 (0 is red). Meaningless for a grey, which has none.
    var hue: Double {
        let high = max(red, green, blue), low = min(red, green, blue)
        let spread = high - low
        guard spread > 0 else { return 0 }
        var degrees: Double
        if high == red {
            degrees = (green - blue) / spread
        } else if high == green {
            degrees = 2 + (blue - red) / spread
        } else {
            degrees = 4 + (red - green) / spread
        }
        degrees *= 60
        return degrees < 0 ? degrees + 360 : degrees
    }

    /// How far apart two hues are on the colour wheel, from 0 to 180 degrees.
    func hueDistance(to other: RGB) -> Double {
        let gap = abs(hue - other.hue)
        return min(gap, 360 - gap)
    }

    /// This colour moved towards another by a fraction.
    func mixed(with other: RGB, by fraction: Double) -> RGB {
        RGB(
            red: red + (other.red - red) * fraction,
            green: green + (other.green - green) * fraction,
            blue: blue + (other.blue - blue) * fraction
        )
    }
}

/// The look of the app, chosen in the Settings window. Kept in the user defaults, so every window
/// shares it and it survives a restart.
enum Theme: String, CaseIterable, Identifiable, Sendable {
    /// Follows macOS: light or dark, and the accent colour of the system.
    case system
    case light
    case dark
    /// Dark, from the colours of the logo: indigo into magenta.
    case helix
    /// Dark and warm: charcoal and an ember orange.
    case ember

    static let defaultsKey = "theme"
    static let `default` = Theme.system

    var id: String { rawValue }

    /// The name the Settings window shows.
    var title: String {
        switch self {
        case .system: String(localized: "System")
        case .light: String(localized: "Light")
        case .dark: String(localized: "Dark")
        case .helix: String(localized: "Helix")
        case .ember: String(localized: "Ember")
        }
    }

    /// The theme chosen, and the system theme before any was chosen or when the stored value is
    /// not one of ours.
    static func stored(in defaults: UserDefaults = .standard) -> Theme {
        defaults.string(forKey: defaultsKey).flatMap(Theme.init(rawValue:)) ?? .default
    }

    func store(in defaults: UserDefaults = .standard) {
        defaults.set(rawValue, forKey: Theme.defaultsKey)
    }

    /// Whether the window is forced to light (false) or dark (true) whatever the system says, or
    /// follows the system (nil).
    var forcedDark: Bool? {
        switch self {
        case .system: nil
        case .light: false
        case .dark, .helix, .ember: true
        }
    }

    /// Whether the interface keeps the colours of macOS (windows, cards, text) and only the
    /// appearance changes. The custom themes paint their own.
    var followsSystemColors: Bool {
        switch self {
        case .system, .light, .dark: true
        case .helix, .ember: false
        }
    }

    /// The palette to draw with. `systemIsDark` is what the system appearance of the window is,
    /// which only the system theme looks at.
    func palette(systemIsDark: Bool) -> ThemePalette {
        switch self {
        case .system: systemIsDark ? .dark : .light
        case .light: .light
        case .dark: .dark
        case .helix: .helix
        case .ember: .ember
        }
    }
}

/// The colours a theme gives to the parts the app draws itself: the viewport, the node editor, the
/// timeline and the windows. Everything is a plain colour so that the pairs can be measured; a
/// theme that follows the system maps most of them to the semantic colours of macOS instead (see
/// `ThemeColors`), and these values are then close approximations of them.
struct ThemePalette: Equatable, Sendable {
    let theme: Theme
    let isDark: Bool
    /// Whether windows, cards and text take the semantic colours of macOS.
    let followsSystemColors: Bool

    // Windows and text.
    let window: RGB
    /// Cards, fields and the timeline bar: what sits on the window.
    let surface: RGB
    let text: RGB
    let textSecondary: RGB
    /// The colour of selection, focus and the playhead. Fixed by the theme, whatever the accent
    /// of macOS is: on a Mac whose accent is red it would be the colour of an error.
    let accent: RGB

    // The node editor.
    let nodeGrid: RGB
    let nodeCardStroke: RGB
    let nodeLink: RGB
    let nodePort: RGB
    /// The flag and the bar of the node the viewport shows. Its own colour, neither the accent
    /// of the selection nor the red of an error.
    let displayFlag: RGB

    // The timeline.
    let rulerLine: RGB
    let cachedBar: RGB
    let keyframe: RGB

    // The viewport.
    let viewportBackground: RGB
    let viewportGeometry: RGB
    /// The lines of the ground grid, which the shader draws half transparent and fading out.
    let viewportGrid: RGB

    /// The red that means an error, and nothing else. The views draw it with the system red; this
    /// is the value the tests measure the other colours against.
    static let errorRed = RGB(hex: 0xFF3B30)

    /// The colour of text and symbols drawn on the accent: whichever of black and white reads
    /// better on it.
    var onAccent: RGB {
        let white = RGB(hex: 0xFFFFFF)
        let black = RGB(hex: 0x000000)
        return accent.contrast(with: white) >= accent.contrast(with: black) ? white : black
    }
}

extension ThemePalette {
    /// The viewport of the app has always been this dark slate, in every appearance, with this grid.
    private static let slate = RGB(red: 0.13, green: 0.14, blue: 0.16)
    private static let facadeGeometry = RGB(red: 0.62, green: 0.64, blue: 0.68)
    private static let facadeGrid = RGB(red: 0.227, green: 0.227, blue: 0.275)

    static let light = ThemePalette(
        theme: .light, isDark: false, followsSystemColors: true,
        window: RGB(hex: 0xECECEC), surface: RGB(hex: 0xFFFFFF),
        text: RGB(hex: 0x1D1D1F), textSecondary: RGB(hex: 0x58585C), accent: RGB(hex: 0x5B4FD6),
        nodeGrid: RGB(hex: 0xD0D0D0), nodeCardStroke: RGB(hex: 0xB4B4B4),
        nodeLink: RGB(hex: 0x767676), nodePort: RGB(hex: 0x737373),
        displayFlag: RGB(hex: 0x0A84D6),
        rulerLine: RGB(hex: 0xA8A8A8), cachedBar: RGB(hex: 0x0A60FF), keyframe: RGB(hex: 0xFF9500),
        viewportBackground: slate, viewportGeometry: facadeGeometry, viewportGrid: facadeGrid
    )

    static let dark = ThemePalette(
        theme: .dark, isDark: true, followsSystemColors: true,
        window: RGB(hex: 0x1E1E1E), surface: RGB(hex: 0x353535),
        text: RGB(hex: 0xF2F2F7), textSecondary: RGB(hex: 0xA1A1A6), accent: RGB(hex: 0x8B7CFF),
        nodeGrid: RGB(hex: 0x3A3A3A), nodeCardStroke: RGB(hex: 0x6A6A6A),
        nodeLink: RGB(hex: 0x8C8C8C), nodePort: RGB(hex: 0xB3B3B3),
        displayFlag: RGB(hex: 0x40B4FF),
        rulerLine: RGB(hex: 0x6A6A6A), cachedBar: RGB(hex: 0x0A84FF), keyframe: RGB(hex: 0xFF9F0A),
        viewportBackground: slate, viewportGeometry: facadeGeometry, viewportGrid: facadeGrid
    )

    /// From the logo: the gradient goes from #212566 (blue violet) through #4D337E to #BD4795
    /// (magenta), so the windows are deep indigo and the accent sits between violet and magenta.
    static let helix = ThemePalette(
        theme: .helix, isDark: true, followsSystemColors: false,
        window: RGB(hex: 0x15122F), surface: RGB(hex: 0x241E4E),
        text: RGB(hex: 0xF0EDFF), textSecondary: RGB(hex: 0xB4AEDC), accent: RGB(hex: 0xC063E0),
        nodeGrid: RGB(hex: 0x3A3274), nodeCardStroke: RGB(hex: 0x5C52A6),
        nodeLink: RGB(hex: 0x9188DA), nodePort: RGB(hex: 0xA9A1EA),
        displayFlag: RGB(hex: 0xFF7FC0),
        rulerLine: RGB(hex: 0x5C52A6), cachedBar: RGB(hex: 0x6C74F0), keyframe: RGB(hex: 0xFF7FC0),
        viewportBackground: RGB(hex: 0x100D26), viewportGeometry: RGB(hex: 0xC8BFF2), viewportGrid: RGB(hex: 0x5448A8)
    )

    /// Charcoal with a warm cast, and the orange of embers.
    static let ember = ThemePalette(
        theme: .ember, isDark: true, followsSystemColors: false,
        window: RGB(hex: 0x1C1917), surface: RGB(hex: 0x2B2622),
        text: RGB(hex: 0xF5EDE6), textSecondary: RGB(hex: 0xB9AA9C), accent: RGB(hex: 0xFF7A1A),
        nodeGrid: RGB(hex: 0x3B342E), nodeCardStroke: RGB(hex: 0x6A5A4D),
        nodeLink: RGB(hex: 0xA8927F), nodePort: RGB(hex: 0xC0AA95),
        displayFlag: RGB(hex: 0x4DD0C8),
        rulerLine: RGB(hex: 0x6A5A4D), cachedBar: RGB(hex: 0xD6482F), keyframe: RGB(hex: 0xFFC247),
        viewportBackground: RGB(hex: 0x14110F), viewportGeometry: RGB(hex: 0xE8D5C4), viewportGrid: RGB(hex: 0x6E5B4A)
    )
}
