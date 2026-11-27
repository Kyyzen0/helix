import AppKit
import Foundation
import SwiftUI

/// `Helix --render-getting-started <out.png> [width] [--dark] [--theme …] [--appearance …] [--language …]`
///
/// Draws the window of Help > Getting Started in an off screen window, in a theme and a language,
/// so its text can be checked without a screen.
@MainActor
enum GettingStartedSnapshotCommand {
    static let flag = "--render-getting-started"

    static let usage = "usage: Helix \(flag) <out.png> [width] [--dark] \(ThemeOptions.usage)"

    static func run(_ all: [String]) -> Int32 {
        guard let (themeOptions, arguments) = ThemeOptions.extract(from: all) else { return OffscreenRender.fail(usage) }
        var positional: [String] = []
        var dark = false
        for argument in arguments {
            if argument == "--dark" { dark = true } else { positional.append(argument) }
        }
        guard positional.count == 1 || positional.count == 2 else { return OffscreenRender.fail(usage) }
        let width = positional.count == 2 ? Double(positional[1]) : 560
        guard let width, width > 0 else { return OffscreenRender.fail(usage) }

        let palette = themeOptions.palette(dark: dark)
        _ = NSApplication.shared
        // The height the window asks for at that width, so the whole text shows.
        let content = GettingStartedView(scrolls: false)
            .frame(width: width, alignment: .top)
            .background(ThemeColors(palette).window)
            .themeEnvironment(palette)
        guard let image = OffscreenRender.image(of: content, width: width, height: nil, dark: palette.isDark) else {
            return OffscreenRender.fail("the window could not be drawn")
        }
        do {
            try OffscreenRender.write(image, to: positional[0])
        } catch {
            return OffscreenRender.fail(error.localizedDescription)
        }
        print("wrote \(positional[0]) (\(image.width) x \(image.height) pixels)")
        return 0
    }
}
