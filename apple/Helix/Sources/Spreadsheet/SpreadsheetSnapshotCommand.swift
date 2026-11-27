import AppKit
import CxxStdlib
import Foundation
import HelixCore
import SwiftUI

/// `Helix --render-spreadsheet <in.kut> <node> <out.png> [width height] [--owner points|vertices|faces|primitives] [--dark]`
///
/// Opens a scene with no window and writes a picture of the spreadsheet of one node in an off
/// screen window: the same table and the same reads of the engine as the app makes.
@MainActor
enum SpreadsheetSnapshotCommand {
    static let flag = "--render-spreadsheet"

    static let usage = "usage: Helix \(flag) <in.kut> <node> <out.png> [width height] [--owner points|vertices|faces|primitives] [--dark]"

    static func run(_ arguments: [String]) -> Int32 {
        var positional: [String] = []
        var owner = SpreadsheetOwner.points
        var dark = false
        var index = 0
        while index < arguments.count {
            switch arguments[index] {
            case "--owner":
                guard index + 1 < arguments.count, let parsed = SpreadsheetOwner(name: arguments[index + 1]) else {
                    return OffscreenRender.fail(usage)
                }
                owner = parsed
                index += 1
            case "--dark":
                dark = true
            default:
                positional.append(arguments[index])
            }
            index += 1
        }
        guard positional.count == 3 || positional.count == 5 else { return OffscreenRender.fail(usage) }
        let width = positional.count == 5 ? Double(positional[3]) : 900
        let height = positional.count == 5 ? Double(positional[4]) : 320
        guard let width, let height, width > 0, height > 0 else { return OffscreenRender.fail(usage) }

        do {
            try EngineHost.shared.startEngine()
            try HelixError.check(helix.core.openScene(std.string(positional[0])))

            let wanted = positional[1]
            let nodes = Array(helix.core.listNodes())
            guard let node = nodes.first(where: { String(engine: $0.path) == wanted })
                ?? nodes.first(where: { String(engine: $0.name) == wanted })
            else { return OffscreenRender.fail("no node named \(wanted)") }

            // The window cooks the display node when it opens, which is when a node learns it failed.
            _ = helix.core.updateViewportGeometry()

            let source = FacadeSpreadsheetSource()
            let model = SpreadsheetModel()
            model.source = source
            model.owner = owner
            model.follow(node: node.id)
            print("\(model.nodeName ?? "?"): \(owner.title) \(model.rowCount) rows, columns \(model.columns.map(\.title).joined(separator: " "))")
            if case .failed(let message) = model.state { print("error: \(message)") }

            let content = SpreadsheetView(model: model).background(Color(nsColor: .windowBackgroundColor))
            guard let image = OffscreenRender.image(of: content, width: width, height: height, dark: dark) else {
                return OffscreenRender.fail("the spreadsheet could not be drawn")
            }
            try OffscreenRender.write(image, to: positional[2])
            print("wrote \(positional[2]) (\(image.width) x \(image.height) pixels), \(model.pagesRead) pages read")
            withExtendedLifetime(source) {}
            return 0
        } catch {
            return OffscreenRender.fail(error.localizedDescription)
        }
    }
}
