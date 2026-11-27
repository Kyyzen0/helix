import Foundation

/// The options every `--render-*` command takes to draw in a theme:
/// `--theme <system|light|dark|helix|ember>` and, for the system theme, `--appearance <light|dark>`.
/// They can stand anywhere among the arguments of a command.
struct ThemeOptions: Equatable {
    static let themeFlag = "--theme"
    static let appearanceFlag = "--appearance"
    static let usage = "[\(themeFlag) <\(Theme.allCases.map(\.rawValue).joined(separator: "|"))>] [\(appearanceFlag) <light|dark>]"

    var theme = Theme.system
    /// The appearance the system theme has, when asked for one.
    var appearanceDark: Bool?

    /// The palette to draw with. `dark` is what the command's own `--dark` flag says; the
    /// appearance option wins over it. A theme that forces light or dark ignores both.
    func palette(dark: Bool = false) -> ThemePalette {
        theme.palette(systemIsDark: appearanceDark ?? dark)
    }

    /// Takes the theme options out of the arguments of a command, and returns the rest, or nil
    /// when an option has no value or a value that is not one of ours.
    static func extract(from arguments: [String]) -> (options: ThemeOptions, rest: [String])? {
        var options = ThemeOptions()
        var rest: [String] = []
        var index = 0
        while index < arguments.count {
            switch arguments[index] {
            case themeFlag:
                guard index + 1 < arguments.count, let theme = Theme(rawValue: arguments[index + 1]) else { return nil }
                options.theme = theme
                index += 1
            case appearanceFlag:
                guard index + 1 < arguments.count, ["light", "dark"].contains(arguments[index + 1]) else { return nil }
                options.appearanceDark = arguments[index + 1] == "dark"
                index += 1
            default:
                rest.append(arguments[index])
            }
            index += 1
        }
        return (options, rest)
    }
}
