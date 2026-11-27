import AppKit
import Foundation
import SwiftUI

/// `Helix --render-settings <out.png> [width] [--tab <appearance|general>] [--choice <system|en|fr>] [--dark] [--theme …] [--appearance …]`
///
/// Draws a tab of the Settings window in an off screen window, so the pickers can be checked
/// without a screen: the Appearance tab with the theme given as the one chosen, or the General
/// tab with the language given as the one chosen (the language the run is in, when none is).
@MainActor
enum SettingsSnapshotCommand {
    static let flag = "--render-settings"

    static let usage = "usage: Helix \(flag) <out.png> [width] [--tab <appearance|general>] [--choice <system|en|fr>] [--dark] \(ThemeOptions.usage)"

    static func run(_ all: [String]) -> Int32 {
        guard let (themeOptions, arguments) = ThemeOptions.extract(from: all) else { return OffscreenRender.fail(usage) }
        var positional: [String] = []
        var dark = false
        var tab = "appearance"
        var choice = Bundle.main.preferredLocalizations.first.flatMap(AppLanguage.init(rawValue:)) ?? .system
        var index = 0
        while index < arguments.count {
            switch arguments[index] {
            case "--dark":
                dark = true
            case "--tab":
                guard index + 1 < arguments.count, ["appearance", "general"].contains(arguments[index + 1]) else { return OffscreenRender.fail(usage) }
                tab = arguments[index + 1]
                index += 1
            case "--choice":
                guard index + 1 < arguments.count, let parsed = AppLanguage(rawValue: arguments[index + 1]) else { return OffscreenRender.fail(usage) }
                choice = parsed
                index += 1
            default:
                positional.append(arguments[index])
            }
            index += 1
        }
        guard positional.count == 1 || positional.count == 2 else { return OffscreenRender.fail(usage) }
        let width = positional.count == 2 ? Double(positional[1]) : 540
        guard let width, width > 0 else { return OffscreenRender.fail(usage) }

        let palette = themeOptions.palette(dark: dark)
        _ = NSApplication.shared
        let image: CGImage?
        if tab == "general" {
            // The language menu is an AppKit control, which only a real window draws.
            let content = SettingsSnapshotCommand.pane(tab: tab, theme: themeOptions.theme, language: choice)
                .frame(width: width, alignment: .top)
                .background(ThemeColors(palette).window)
                .themeEnvironment(palette)
            image = OffscreenRender.image(of: content, width: width, height: nil, dark: palette.isDark)
        } else {
            // The pane holds no AppKit control, so ImageRenderer draws it without a window.
            let content = SettingsSnapshotCommand.pane(tab: tab, theme: themeOptions.theme, language: choice)
                .frame(width: width, height: 215, alignment: .top)
                .background(ThemeColors(palette).window)
                .themeEnvironment(palette)
                .environment(\.colorScheme, palette.isDark ? .dark : .light)
            let renderer = ImageRenderer(content: content)
            renderer.scale = 2
            image = renderer.cgImage
        }
        guard let image else { return OffscreenRender.fail("the settings could not be drawn") }
        do {
            try OffscreenRender.write(image, to: positional[0])
        } catch {
            return OffscreenRender.fail(error.localizedDescription)
        }
        print("wrote \(positional[0]) (\(image.width) x \(image.height) pixels)")
        return 0
    }

    @ViewBuilder
    private static func pane(tab: String, theme: Theme, language: AppLanguage) -> some View {
        if tab == "general" {
            LanguagePane(
                selection: .constant(language),
                needsRelaunch: AppLanguage.needsRelaunch(choice: language),
                relaunch: {}
            )
        } else {
            AppearancePane(selection: .constant(theme))
        }
    }
}
