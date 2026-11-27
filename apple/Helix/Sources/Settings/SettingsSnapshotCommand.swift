import AppKit
import Foundation
import SwiftUI

/// `Helix --render-settings <out.png> [width] [--dark] [--theme …] [--appearance …]`
///
/// Draws the Appearance tab of the Settings window in an off screen window, with the theme
/// given as the one chosen, so the picker can be checked without a screen.
@MainActor
enum SettingsSnapshotCommand {
    static let flag = "--render-settings"

    static let usage = "usage: Helix \(flag) <out.png> [width] [--dark] \(ThemeOptions.usage)"

    static func run(_ all: [String]) -> Int32 {
        guard let (themeOptions, arguments) = ThemeOptions.extract(from: all) else { return OffscreenRender.fail(usage) }
        var positional: [String] = []
        var dark = false
        for argument in arguments {
            if argument == "--dark" { dark = true } else { positional.append(argument) }
        }
        guard positional.count == 1 || positional.count == 2 else { return OffscreenRender.fail(usage) }
        let width = positional.count == 2 ? Double(positional[1]) : 540
        guard let width, width > 0 else { return OffscreenRender.fail(usage) }

        let palette = themeOptions.palette(dark: dark)
        // The pane holds no AppKit control, so ImageRenderer draws it without a window.
        _ = NSApplication.shared
        let content = AppearancePane(selection: .constant(themeOptions.theme))
            .frame(width: width, height: 215, alignment: .top)
            .background(ThemeColors(palette).window)
            .themeEnvironment(palette)
            .environment(\.colorScheme, palette.isDark ? .dark : .light)
        let renderer = ImageRenderer(content: content)
        renderer.scale = 2
        guard let image = renderer.cgImage else { return OffscreenRender.fail("the settings could not be drawn") }
        do {
            try OffscreenRender.write(image, to: positional[0])
        } catch {
            return OffscreenRender.fail(error.localizedDescription)
        }
        print("wrote \(positional[0]) (\(image.width) x \(image.height) pixels)")
        return 0
    }
}
