import AppKit
import SwiftUI

extension Color {
    init(_ rgb: RGB) {
        self.init(.sRGB, red: rgb.red, green: rgb.green, blue: rgb.blue, opacity: 1)
    }
}

extension NSColor {
    convenience init(_ rgb: RGB) {
        self.init(srgbRed: rgb.red, green: rgb.green, blue: rgb.blue, alpha: 1)
    }
}

/// A palette as SwiftUI colours. A theme that follows the system keeps the semantic colours of
/// macOS (so Light, Dark and System look as they always did, and the accent colour is the
/// system's), a custom theme paints its own.
struct ThemeColors: Sendable {
    let palette: ThemePalette

    init(_ palette: ThemePalette) { self.palette = palette }

    private var system: Bool { palette.followsSystemColors }
    private var dark: Bool { palette.isDark }

    /// The accent colour: the system's, or the one of the theme.
    var accent: Color { palette.accent.map(Color.init) ?? Color.accentColor }
    /// What is drawn on the accent: a label on a chip, the glyph of a button that is on.
    var onAccent: Color { Color(palette.onAccent) }
    /// The colour of the display flag and of the bar down the card of the display node.
    var displayFlag: Color { system ? accent : Color(palette.displayFlag) }

    var window: Color { system ? Color(nsColor: .windowBackgroundColor) : Color(palette.window) }
    /// Cards, and the room the timeline sits in.
    var surface: Color {
        guard system else { return Color(palette.surface) }
        return dark
            ? Color(nsColor: .windowBackgroundColor).mix(with: .white, by: 0.1)
            : Color(nsColor: .controlBackgroundColor)
    }
    /// The background of the timeline bar.
    var bar: AnyShapeStyle { system ? AnyShapeStyle(.bar) : AnyShapeStyle(Color(palette.window.mixed(with: palette.surface, by: 0.5))) }

    var text: Color { system ? Color.primary : Color(palette.text) }
    var textSecondary: Color { system ? Color.secondary : Color(palette.textSecondary) }

    // The node editor.
    var nodeBackground: Color { window }
    var nodeGrid: Color { system ? Color.primary.opacity(0.14) : Color(palette.nodeGrid) }
    var nodeCardStroke: Color { system ? Color.primary.opacity(0.28) : Color(palette.nodeCardStroke) }
    var nodeLink: Color { system ? Color.primary.opacity(0.5) : Color(palette.nodeLink) }
    /// Opaque, so a link that ends in a port does not show through it.
    var nodePort: Color { system ? Color(white: dark ? 0.7 : 0.45) : Color(palette.nodePort) }

    // The timeline.
    /// The room around the range, the range, and the lines of the ruler.
    var rulerRoom: Color { system ? Color.primary.opacity(0.05) : Color(palette.window.mixed(with: palette.text, by: 0.04)) }
    var rulerRange: Color { system ? Color.primary.opacity(0.07) : Color(palette.window.mixed(with: palette.text, by: 0.1)) }
    var rulerLine: Color { system ? Color.primary.opacity(0.35) : Color(palette.rulerLine) }
    var cachedBar: Color { system ? Color.blue.opacity(0.85) : Color(palette.cachedBar) }
    var keyframe: Color { system ? Color.orange : Color(palette.keyframe) }
    /// The outline of a diamond.
    var keyframeEdge: Color { Color.black.opacity(0.35) }
    var playhead: Color { accent }
}

private struct ThemeColorsKey: EnvironmentKey {
    static let defaultValue = ThemeColors(.light)
}

extension EnvironmentValues {
    /// The colours of the theme of the window.
    var theme: ThemeColors {
        get { self[ThemeColorsKey.self] }
        set { self[ThemeColorsKey.self] = newValue }
    }
}

extension View {
    /// Puts a palette in the environment, and tints the controls with its accent.
    func themeEnvironment(_ palette: ThemePalette) -> some View {
        environment(\.theme, ThemeColors(palette))
            .tint(palette.accent.map(Color.init))
    }

    /// Makes the view follow the theme chosen in the Settings window, live.
    func themed() -> some View { modifier(ThemedModifier()) }
}

/// Reads the theme from the user defaults, so a change in the Settings window reaches every
/// window at once, and applies it: the palette for the views, the tint, and the colour behind the
/// windows.
private struct ThemedModifier: ViewModifier {
    @AppStorage(Theme.defaultsKey) private var stored = Theme.default.rawValue
    @Environment(\.colorScheme) private var scheme

    func body(content: Content) -> some View {
        let theme = Theme(rawValue: stored) ?? .default
        let palette = theme.palette(systemIsDark: scheme == .dark)
        content
            .themeEnvironment(palette)
            .background(WindowTheme(palette: palette))
            // The theme may have been written by something other than the Settings window.
            .onChange(of: stored, initial: true) { ThemeAppearance.apply(theme) }
    }
}

/// Sets the colour behind the content of the window of this view, so the parts SwiftUI leaves
/// transparent (the gaps of the split view, the inspector column) are the theme's.
private struct WindowTheme: NSViewRepresentable {
    let palette: ThemePalette

    func makeNSView(context: Context) -> NSView { NSView() }

    func updateNSView(_ view: NSView, context: Context) {
        let color: NSColor? = palette.followsSystemColors ? nil : NSColor(palette.window)
        DispatchQueue.main.async { view.window?.backgroundColor = color ?? .windowBackgroundColor }
    }
}

/// The appearance of the whole app, which is where the dark and light of a theme come from: it
/// makes SwiftUI, AppKit controls and the title bars follow, in every window at once.
@MainActor
enum ThemeAppearance {
    /// An appearance that wins over the theme and the system, for the commands that draw a window
    /// in a given appearance (`--render-window`). Nil in the app.
    static var override: NSAppearance?

    static func apply(_ theme: Theme) {
        let appearance: NSAppearance? = override
            ?? theme.forcedDark.map { NSAppearance(named: $0 ? .darkAqua : .aqua) } ?? nil
        guard NSApplication.shared.appearance?.name != appearance?.name else { return }
        NSApplication.shared.appearance = appearance
    }

    /// The theme stored, applied before the first window opens.
    static func applyStored() { apply(Theme.stored()) }
}

extension NSAppearance {
    var isDark: Bool { bestMatch(from: [.darkAqua, .aqua]) == .darkAqua }
}
