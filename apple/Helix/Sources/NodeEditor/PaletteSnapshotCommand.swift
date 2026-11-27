import AppKit
import CxxStdlib
import Foundation
import HelixCore
import SwiftUI

/// `Helix --render-palette <query> <out.png> [width height] [--dark] [--theme …] [--language …]`
///
/// Draws the node palette with a query typed in it ("" for none), over the node types the engine
/// has, in an off screen window. It is how the labels and the search of the palette are checked
/// in each language without a screen.
@MainActor
enum PaletteSnapshotCommand {
    static let flag = "--render-palette"

    static let usage = "usage: Helix \(flag) <query> <out.png> [width height] [--dark] \(ThemeOptions.usage)"

    static func run(_ all: [String]) -> Int32 {
        guard let (themeOptions, arguments) = ThemeOptions.extract(from: all) else { return OffscreenRender.fail(usage) }
        var positional: [String] = []
        var dark = false
        for argument in arguments {
            if argument == "--dark" { dark = true } else { positional.append(argument) }
        }
        guard positional.count == 2 || positional.count == 4 else { return OffscreenRender.fail(usage) }
        let width = positional.count == 4 ? Double(positional[2]) : 400
        let height = positional.count == 4 ? Double(positional[3]) : 420
        guard let width, let height, width > 0, height > 0 else { return OffscreenRender.fail(usage) }

        do {
            try EngineHost.shared.startEngine()
            let model = NodeEditorModel()
            model.paletteTypes = NodeTypeCatalog.all
            let request = PaletteRequest(anchor: CGPoint(x: 20, y: 10), canvas: .zero)
            let palette = themeOptions.palette(dark: dark)
            let view = NodePalette(model: model, request: request, area: CGSize(width: width, height: height), query: positional[0])
                .frame(width: width, height: height, alignment: .topLeading)
                .background(ThemeColors(palette).window)
                .themeEnvironment(palette)
            guard let image = OffscreenRender.image(of: view, width: width, height: height, dark: palette.isDark) else {
                return OffscreenRender.fail("the palette could not be drawn")
            }
            try OffscreenRender.write(image, to: positional[1])
            let results = NodeSearch.rank(positional[0], in: model.paletteTypes).prefix(8)
            print(results.map { "\($0.type.label) (\($0.score))" }.joined(separator: ", "))
            print("wrote \(positional[1]) (\(image.width) x \(image.height) pixels)")
            return 0
        } catch {
            return OffscreenRender.fail(error.localizedDescription)
        }
    }
}
