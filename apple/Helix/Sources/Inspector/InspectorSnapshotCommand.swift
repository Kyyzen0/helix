import AppKit
import CxxStdlib
import Foundation
import HelixCore
import ImageIO
import SwiftUI
import UniformTypeIdentifiers

/// `Helix --render-inspector <in.kut | --new-scene> <node name or path> <out.png> [width [height]] [--dark]`
///
/// Opens a scene with no window, cooks it as the window would, and writes a picture of the
/// inspector for one node in an off screen window. It is how the drawing of the inspector is checked
/// without a screen: the same views, over the same facade calls. The picture is of the panel as
/// it is when the node is selected, so nothing in it can be changed.
@MainActor
enum InspectorSnapshotCommand {
    static let flag = "--render-inspector"

    static let usage = "usage: Helix \(flag) <in.kut | \(SnapshotCommand.newSceneArgument)> <node name or path> <out.png> [width [height]] [--dark] \(ThemeOptions.usage)"

    /// What an inspector with no document behind it does: it reads the engine and edits nothing.
    @MainActor
    private final class ReadOnlySource: InspectorEditing {
        func loadInspector(node: UInt64) -> InspectorNode? { InspectorLoader.load(node: node) }
        func attributeSuggestions(node: UInt64, parameter: String) -> [String] { [] }
        func apply(_ edit: InspectorEdit, to node: UInt64, named name: String) -> EditOutcome { .done }
        func applyLive(_ edit: InspectorEdit, to node: UInt64) -> EditOutcome { .done }
        func beginContinuousEdit(named name: String) {}
        func endContinuousEdit() {}
        func setDisplay(_ node: UInt64) -> EditOutcome { .done }
    }

    static func run(_ all: [String]) -> Int32 {
        guard let (themeOptions, arguments) = ThemeOptions.extract(from: all) else { return fail(usage) }
        var positional: [String] = []
        var dark = false
        for argument in arguments {
            if argument == "--dark" { dark = true } else { positional.append(argument) }
        }
        guard (3...5).contains(positional.count) else { return fail(usage) }
        let width = positional.count >= 4 ? Double(positional[3]) : 320
        // Without a height the panel is as tall as its rows.
        let height: Double? = positional.count == 5 ? Double(positional[4]) : nil
        guard let width, width > 0, positional.count < 5 || (height ?? 0) > 0 else { return fail(usage) }

        do {
            try EngineHost.shared.startEngine()
            if positional[0] == SnapshotCommand.newSceneArgument {
                try EngineHost.shared.makeDefaultScene()
            } else {
                try HelixError.check(helix.core.openScene(std.string(positional[0])))
            }

            let wanted = positional[1]
            let nodes = Array(helix.core.listNodes())
            guard let node = nodes.first(where: { String(engine: $0.path) == wanted })
                ?? nodes.first(where: { String(engine: $0.name) == wanted })
            else {
                return fail("no node named \(wanted)")
            }

            // The window cooks the scene when it opens, which is when a node learns that it
            // failed. The node itself is cooked too, since it may not be what is displayed.
            _ = helix.core.updateViewportGeometry()
            _ = helix.core.cookNodeReport(node.id)

            let source = ReadOnlySource()
            let model = InspectorModel()
            model.editing = source
            model.select(nodes: [node.id])
            guard case .node(let loaded) = model.content else { return fail("the node could not be read") }

            // The app has not started, but AppKit needs to exist to draw controls and symbols.
            _ = NSApplication.shared
            // The controls of the panel are AppKit's (fields, sliders, menus), which ImageRenderer
            // draws as a placeholder, so the panel is laid out in a window that is never shown
            // and its pixels are read back.
            let palette = themeOptions.palette(dark: dark)
            let content = InspectorView(model: model, scrolls: false)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
                .background(ThemeColors(palette).window)
                .themeEnvironment(palette)
            guard let image = draw(content, width: width, height: height, dark: palette.isDark) else {
                return fail("the inspector could not be drawn")
            }
            try write(image, to: positional[2])
            print("\(loaded.name) (\(loaded.typeLabel)): \(loaded.parameters.count) parameters, \(model.entries.count) rows")
            if let error = loaded.error { print("error: \(error)") }
            print("wrote \(positional[2]) (\(image.width) x \(image.height) pixels, \(Int(width)) points wide)")
            withExtendedLifetime(source) {}
            return 0
        } catch {
            return fail(error.localizedDescription)
        }
    }

    /// Draws a view in an off screen window, as tall as the rows need when no height is given.
    private static func draw<Content: View>(_ content: Content, width: Double, height: Double?, dark: Bool) -> CGImage? {
        let host = NSHostingView(rootView: content)
        host.frame = NSRect(x: 0, y: 0, width: width, height: height ?? 100)
        let window = NSWindow(contentRect: host.frame, styleMask: [.borderless], backing: .buffered, defer: false)
        window.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
        window.contentView = host
        // Fields and menus finish setting themselves up on the run loop, as they do on screen.
        for _ in 0..<4 {
            host.layoutSubtreeIfNeeded()
            RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.15))
        }
        // The height the rows ask for at this width, from SwiftUI itself.
        let natural = NSHostingController(rootView: content).sizeThatFits(in: CGSize(width: width, height: 20_000)).height
        let size = NSSize(width: width, height: height ?? ceil(natural))
        window.setContentSize(size)
        host.frame = NSRect(origin: .zero, size: size)
        RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.2))
        host.layoutSubtreeIfNeeded()
        guard let representation = host.bitmapImageRepForCachingDisplay(in: host.bounds) else { return nil }
        host.cacheDisplay(in: host.bounds, to: representation)
        guard let image = representation.cgImage else { return nil }
        return height == nil ? trimmed(image) : image
    }

    /// The picture without the empty window background under the last row, which SwiftUI's
    /// estimate of the height of the rows overshoots.
    private static func trimmed(_ image: CGImage) -> CGImage {
        guard let data = image.dataProvider?.data, let bytes = CFDataGetBytePtr(data) else { return image }
        let bytesPerPixel = image.bitsPerPixel / 8
        guard bytesPerPixel >= 3 else { return image }
        let background = (0..<bytesPerPixel).map { bytes[$0] }
        var last = image.height - 1
        rows: while last > 0 {
            for x in 0..<image.width {
                let offset = last * image.bytesPerRow + x * bytesPerPixel
                for channel in 0..<bytesPerPixel where abs(Int(bytes[offset + channel]) - Int(background[channel])) > 3 {
                    break rows
                }
            }
            last -= 1
        }
        let keep = min(image.height, last + 1 + 24)
        return image.cropping(to: CGRect(x: 0, y: 0, width: image.width, height: keep)) ?? image
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
