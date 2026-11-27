import AppKit
import CxxStdlib
import Foundation
import HelixCore
import SwiftUI

/// `Helix --render-timeline <in.kut> <out.png> [width] [--select <node>] [--frame <n>] [--dark]`
///
/// Opens a scene with no window and writes a picture of the timeline bar in an off screen window: the
/// controls, the ruler, the playhead and the diamonds of one node (the one named, or the node the
/// scene displays). It is how the drawing of the timeline is checked without a screen.
@MainActor
enum TimelineSnapshotCommand {
    static let flag = "--render-timeline"

    static let usage = "usage: Helix \(flag) <in.kut> <out.png> [width] [--select <node>] [--frame <n>] [--dark]"

    static func run(_ arguments: [String]) -> Int32 {
        var positional: [String] = []
        var selected: String?
        var frame: Float?
        var dark = false
        var index = 0
        while index < arguments.count {
            switch arguments[index] {
            case "--select":
                guard index + 1 < arguments.count else { return OffscreenRender.fail(usage) }
                selected = arguments[index + 1]
                index += 1
            case "--frame":
                guard index + 1 < arguments.count, let value = Float(arguments[index + 1]) else { return OffscreenRender.fail(usage) }
                frame = value
                index += 1
            case "--dark":
                dark = true
            default:
                positional.append(arguments[index])
            }
            index += 1
        }
        guard positional.count == 2 || positional.count == 3 else { return OffscreenRender.fail(usage) }
        let width = positional.count == 3 ? Double(positional[2]) : 900
        guard let width, width > 0 else { return OffscreenRender.fail(usage) }

        do {
            try EngineHost.shared.startEngine()
            try HelixError.check(helix.core.openScene(std.string(positional[0])))
            if let frame { helix.core.setFrame(frame) }

            var keyNode = helix.core.displayNode()
            if let selected {
                let nodes = Array(helix.core.listNodes())
                guard let node = nodes.first(where: { String(engine: $0.path) == selected })
                    ?? nodes.first(where: { String(engine: $0.name) == selected })
                else { return OffscreenRender.fail("no node named \(selected)") }
                keyNode = node.id
            }

            let source = ReadOnlyTimelineSource()
            let model = TimelineModel()
            model.editing = source
            model.setKeyNode(keyNode == 0 ? nil : keyNode)
            model.reload()
            let time = model.time
            print("frame \(NumberText.format(time.frame)) of \(time.start) to \(time.end) at \(NumberText.format(time.fps)) fps, \(model.diamonds.count) diamonds")
            for diamond in model.diamonds { print("  " + diamond.tooltip.replacingOccurrences(of: "\n", with: "; ")) }

            let content = TimelineBar(model: model).background(Color(nsColor: .windowBackgroundColor))
            guard let image = OffscreenRender.image(of: content, width: width, height: nil, dark: dark) else {
                return OffscreenRender.fail("the timeline could not be drawn")
            }
            try OffscreenRender.write(image, to: positional[1])
            print("wrote \(positional[1]) (\(image.width) x \(image.height) pixels)")
            withExtendedLifetime(source) {}
            return 0
        } catch {
            return OffscreenRender.fail(error.localizedDescription)
        }
    }
}
