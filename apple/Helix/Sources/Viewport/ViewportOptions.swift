import Foundation

/// The options `--render-window` and `--render-snapshot` take to look at the scene from somewhere:
/// `--view <home|front|back|right|left|top|bottom>` `--background <theme|dark|medium|light>` and `--navigation <trackpad|mouse|blender>` (the preset the legend of the window shows).
/// They can stand anywhere among the arguments of a command.
struct ViewportOptions: Equatable {
    static let viewFlag = "--view"
    static let backgroundFlag = "--background"
    static let navigationFlag = "--navigation"
    static let usage = "[\(viewFlag) <\(CameraView.argumentNames)>] [\(backgroundFlag) <\(ViewportBackground.allCases.map(\.rawValue).joined(separator: "|"))>] [\(navigationFlag) <\(NavigationPreset.allCases.map(\.rawValue).joined(separator: "|"))>]" // l10n-ok: command line usage

    /// Where the camera looks from. Home, which is where a scene starts, when none was asked for.
    var view = CameraView.home
    /// The background of the viewport, or nil to leave it as the preferences have it.
    var background: ViewportBackground?
    /// The navigation preset, or nil to leave it as the preferences have it.
    var navigation: NavigationPreset?

    /// Takes the options out of the arguments of a command, and returns the rest, or nil when an
    /// option has no value or a value that is not one of ours.
    static func extract(from arguments: [String]) -> (options: ViewportOptions, rest: [String])? {
        var options = ViewportOptions()
        var rest: [String] = []
        var index = 0
        while index < arguments.count {
            switch arguments[index] {
            case viewFlag:
                guard index + 1 < arguments.count, let view = CameraView(argument: arguments[index + 1]) else { return nil }
                options.view = view
                index += 1
            case backgroundFlag:
                guard index + 1 < arguments.count, let background = ViewportBackground(rawValue: arguments[index + 1]) else { return nil }
                options.background = background
                index += 1
            case navigationFlag:
                guard index + 1 < arguments.count, let preset = NavigationPreset(rawValue: arguments[index + 1]) else { return nil }
                options.navigation = preset
                index += 1
            default:
                rest.append(arguments[index])
            }
            index += 1
        }
        return (options, rest)
    }
}
