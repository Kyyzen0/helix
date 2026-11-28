import AppKit
import CxxStdlib
import Foundation
import HelixCore
import ImageIO
import SwiftUI
import UniformTypeIdentifiers

/// `Helix --render-node-editor <in.kut | --new-scene> <out.png> [width height] [--select <name>]... [--add <type>]... [--save <out.kut>] [--tidy] [--hover <node>:<card|eye|in:N|out:N>] [--rename <node>] [--dark]`
///
/// Opens a scene with no window, lays the node editor out as the window does (every node framed)
/// and writes a picture of it with `ImageRenderer`. It is how the drawing of the editor is
/// checked without a screen: the same canvas, over the same facade calls.
///
/// With `--add helix::grid` (as many times as wanted) the scene is a new empty one and the nodes
/// are added the way the Add menu adds them, through the editor of a document: named from the
/// label in the language of the launch (`--language fr`), placed and wired under the selected
/// node, and the last one selected. `--save` writes the scene, so another command can read it.
///
/// `--tidy` lays the graph out as Node > Tidy Graph does (the selected nodes when two or more are
/// selected), `--hover` puts the pointer on a card, its eye or one of its ports (numbered from 0),
/// and `--rename` opens the name field over a card, which is drawn in an off screen window since
/// it is an AppKit control.
@MainActor
enum NodeEditorSnapshotCommand {
    static let flag = "--render-node-editor"

    static let usage = "usage: Helix \(flag) <in.kut | \(SnapshotCommand.newSceneArgument)> <out.png> [width height] [--select <name>]... [--add <type>]... [--save <out.kut>] [--tidy] [--hover <node>:<card|eye|in:N|out:N>] [--rename <node>] [--dark] \(ThemeOptions.usage)"

    static func run(_ all: [String]) -> Int32 {
        guard let (themeOptions, arguments) = ThemeOptions.extract(from: all) else { return fail(usage) }
        var positional: [String] = []
        var selected: [String] = []
        var added: [String] = []
        var savePath: String?
        var hoverSpec: String?
        var renameName: String?
        var tidy = false
        var dark = false
        var index = 0
        while index < arguments.count {
            switch arguments[index] {
            case "--select":
                guard index + 1 < arguments.count else { return fail(usage) }
                selected.append(arguments[index + 1])
                index += 1
            case "--add":
                guard index + 1 < arguments.count else { return fail(usage) }
                added.append(arguments[index + 1])
                index += 1
            case "--save":
                guard index + 1 < arguments.count else { return fail(usage) }
                savePath = arguments[index + 1]
                index += 1
            case "--hover":
                guard index + 1 < arguments.count else { return fail(usage) }
                hoverSpec = arguments[index + 1]
                index += 1
            case "--rename":
                guard index + 1 < arguments.count else { return fail(usage) }
                renameName = arguments[index + 1]
                index += 1
            case "--tidy":
                tidy = true
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
            // The editor of the picture: a document's own when nodes are to be added, so that they
            // are added the way the window adds them, and a bare one otherwise.
            var document: HelixDocument?
            if !added.isEmpty {
                guard positional[0] == SnapshotCommand.newSceneArgument else { return fail("--add needs \(SnapshotCommand.newSceneArgument)") }
                let opened = HelixDocument()
                try opened.activate()
                for name in added {
                    guard let type = opened.editor.paletteTypes.first(where: { $0.name == name }) else {
                        return fail("no node type named \(name)")
                    }
                    opened.editor.addNode(type)
                    if let message = opened.editor.message { return fail(message) }
                }
                document = opened
            } else if positional[0] == SnapshotCommand.newSceneArgument {
                try EngineHost.shared.makeDefaultScene()
            } else {
                try HelixError.check(helix.core.openScene(std.string(positional[0])))
            }

            // The window cooks the scene when it opens, which is when a node learns that it failed.
            _ = helix.core.updateViewportGeometry()
            if let savePath { try HelixError.check(helix.core.saveScene(std.string(savePath))) }

            let model = document?.editor ?? NodeEditorModel()
            model.reload(EditorGraphLoader.current(), frame: true)
            model.setViewSize(CGSize(width: width, height: height))
            let wanted = Set(selected)
            if !wanted.isEmpty || document == nil {
                model.select(nodes: Set(model.graph.nodes.filter { wanted.contains($0.name) }.map(\.id)))
            }
            let missing = wanted.subtracting(model.graph.nodes.map(\.name))
            if !missing.isEmpty { return fail("no node named \(missing.sorted().joined(separator: ", "))") }
            if tidy {
                // The positions the menu command would write, written straight to the scene.
                let part = model.selectedNodes.count >= 2 ? model.selectedNodes : nil
                for move in GraphTidy.moves(for: model.graph, among: part) {
                    try HelixError.check(helix.core.moveNode(move.id, Float(move.position.x), Float(move.position.y)))
                }
                model.reload(EditorGraphLoader.current())
                if part == nil { model.frameAll() } else { model.frameSelection() }
            }
            if let hoverSpec { try apply(hover: hoverSpec, to: model) }
            if let renameName {
                guard let node = model.graph.nodes.first(where: { $0.name == renameName }) else { return fail("no node named \(renameName)") }
                model.beginRename(node.id)
            }

            // The app has not started, but AppKit needs to exist to draw colours and symbols.
            _ = NSApplication.shared
            let palette = themeOptions.palette(dark: dark)
            let image: CGImage
            if let session = model.renaming {
                // A text field is an AppKit control, which ImageRenderer draws as a placeholder.
                let view = ZStack(alignment: .topLeading) {
                    NodeEditorCanvas(model: model)
                    NodeRenameField(model: model, session: session)
                }
                .frame(width: width, height: height)
                .themeEnvironment(palette)
                guard let drawn = OffscreenRender.image(of: view, width: width, height: height, dark: palette.isDark) else {
                    return fail("the editor could not be drawn")
                }
                image = drawn
            } else {
                let view = NodeEditorCanvas(model: model)
                    .frame(width: width, height: height)
                    .themeEnvironment(palette)
                    .environment(\.colorScheme, palette.isDark ? .dark : .light)
                let renderer = ImageRenderer(content: view)
                renderer.scale = 2
                guard let drawn = renderer.cgImage else { return fail("the editor could not be drawn") }
                image = drawn
            }
            try write(image, to: positional[1])
            print("\(model.graph.nodes.count) nodes, \(model.graph.links.count) links")
            print("wrote \(positional[1]) (\(Int(width)) x \(Int(height)) points, \(image.width) x \(image.height) pixels)")
            return 0
        } catch {
            return fail(error.localizedDescription)
        }
    }

    /// Puts the pointer on a card, its eye or a port, as `<node>:<card|eye|in:N|out:N>` says.
    private static func apply(hover spec: String, to model: NodeEditorModel) throws {
        let parts = spec.split(separator: ":").map(String.init)
        guard parts.count >= 2, let node = model.graph.nodes.first(where: { $0.name == parts[0] }) else {
            throw HelixError.engine("--hover wants <node>:<card|eye|in:N|out:N>, and a node of the scene.")
        }
        var hover = EditorHover(node: node.id)
        let index = parts.count > 2 ? Int(parts[2]) ?? 0 : 0
        switch parts[1] {
        case "eye": hover.eye = true
        case "in": hover.port = .input(node: node.id, index: index)
        case "out": hover.port = .output(node: node.id, index: index)
        case "card": break
        default: throw HelixError.engine("--hover wants <node>:<card|eye|in:N|out:N>.")
        }
        model.hover = hover
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
