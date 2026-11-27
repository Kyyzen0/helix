import AppKit
import CxxStdlib
import Foundation
import HelixCore
import ImageIO
import SwiftUI
import UniformTypeIdentifiers

/// `Helix --render-node-editor <in.kut | --new-scene> <out.png> [width height] [--select <name>]... [--dark]`
///
/// Opens a scene with no window, lays the node editor out as the window does (every node framed)
/// and writes a picture of it with `ImageRenderer`. It is how the drawing of the editor is
/// checked without a screen: the same canvas, over the same facade calls.
@MainActor
enum NodeEditorSnapshotCommand {
    static let flag = "--render-node-editor"

    static let usage = "usage: Helix \(flag) <in.kut | \(SnapshotCommand.newSceneArgument)> <out.png> [width height] [--select <name>]... [--dark] \(ThemeOptions.usage)"

    static func run(_ all: [String]) -> Int32 {
        guard let (themeOptions, arguments) = ThemeOptions.extract(from: all) else { return fail(usage) }
        var positional: [String] = []
        var selected: [String] = []
        var dark = false
        var index = 0
        while index < arguments.count {
            switch arguments[index] {
            case "--select":
                guard index + 1 < arguments.count else { return fail(usage) }
                selected.append(arguments[index + 1])
                index += 1
            case "--dark":
                dark = true
            default:
                positional.append(arguments[index])
            }
            index += 1
        }
        guard positional.count == 2 || positional.count == 4 else { return fail(usage) }
        let width = positional.count == 4 ? Double(positional[2]) : 1000
        let height = positional.count == 4 ? Double(positional[3]) : 600
        guard let width, let height, width > 0, height > 0 else { return fail(usage) }

        do {
            try EngineHost.shared.startEngine()
            if positional[0] == SnapshotCommand.newSceneArgument {
                try EngineHost.shared.makeDefaultScene()
            } else {
                try HelixError.check(helix.core.openScene(std.string(positional[0])))
            }

            // The window cooks the scene when it opens, which is when a node learns that it failed.
            _ = helix.core.updateViewportGeometry()

            let model = NodeEditorModel()
            model.reload(EditorGraphLoader.current(), frame: true)
            model.setViewSize(CGSize(width: width, height: height))
            let wanted = Set(selected)
            model.select(nodes: Set(model.graph.nodes.filter { wanted.contains($0.name) }.map(\.id)))
            let missing = wanted.subtracting(model.graph.nodes.map(\.name))
            if !missing.isEmpty { return fail("no node named \(missing.sorted().joined(separator: ", "))") }

            // The app has not started, but AppKit needs to exist to draw colours and symbols.
            _ = NSApplication.shared
            let palette = themeOptions.palette(dark: dark)
            let view = NodeEditorCanvas(model: model)
                .frame(width: width, height: height)
                .themeEnvironment(palette)
                .environment(\.colorScheme, palette.isDark ? .dark : .light)
            let renderer = ImageRenderer(content: view)
            renderer.scale = 2
            guard let image = renderer.cgImage else { return fail("the editor could not be drawn") }
            try write(image, to: positional[1])
            print("\(model.graph.nodes.count) nodes, \(model.graph.links.count) links")
            print("wrote \(positional[1]) (\(Int(width)) x \(Int(height)) points, \(image.width) x \(image.height) pixels)")
            return 0
        } catch {
            return fail(error.localizedDescription)
        }
    }

    private static func write(_ image: CGImage, to path: String) throws {
        let url = URL(fileURLWithPath: path)
        guard let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil) else {
            throw HelixError.engine("Couldn't write \(path).")
        }
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination) else { throw HelixError.engine("Couldn't write \(path).") }
    }

    private static func fail(_ message: String) -> Int32 {
        FileHandle.standardError.write(Data("Helix: \(message)\n".utf8))
        return 1
    }
}
