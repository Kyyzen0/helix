import AppKit
import CxxStdlib
import Foundation
import HelixCore
import SwiftUI

/// `Helix --render-window <in.kut | 3D file | --new-scene> <out.png> [width height] [--pane network|spreadsheet]
/// [--select <node>] [--size small|medium|large] [--theme ...] [--appearance light|dark] [--language ...]`
///
/// Lays out the whole document window (the title bar and the toolbar included) off screen at a
/// size, as the app does, and writes a picture of it. The Metal viewport is the one thing that is
/// not the window's own: a snapshot of a window does not read a Metal layer back, so the picture
/// of the viewport is made by the engine at the size the layout gave it and put in its place.
/// Nothing is written to the preferences of the user: the choices ride in the argument domain.
@MainActor
enum RenderWindowSnapshotCommand {
    static let flag = "--render-window"

    static let usage = "usage: Helix \(flag) <in.kut | 3D file | \(SnapshotCommand.newSceneArgument)> <out.png> [width height] [--pane network|spreadsheet] [--select <node>] [--size small|medium|large] [--show-inspector yes|no] [--example cube|waves|terrain] [--add <node type>]... \(ThemeOptions.usage)"

    static func run(_ all: [String]) -> Int32 {
        guard let (themeOptions, arguments) = ThemeOptions.extract(from: all) else { return OffscreenRender.fail(usage) }
        var positional: [String] = []
        var pane = TimelineSetting.LowerPane.network
        var selected: [String] = []
        var size = InterfaceSize.default
        var inspector = true
        var example: StarterExample?
        var added: [String] = []
        var index = 0
        while index < arguments.count {
            switch arguments[index] {
            case "--pane":
                guard index + 1 < arguments.count, let parsed = TimelineSetting.LowerPane(rawValue: arguments[index + 1]) else {
                    return OffscreenRender.fail(usage)
                }
                pane = parsed
                index += 1
            case "--select":
                guard index + 1 < arguments.count else { return OffscreenRender.fail(usage) }
                selected.append(arguments[index + 1])
                index += 1
            case "--size":
                guard index + 1 < arguments.count, let parsed = InterfaceSize(rawValue: arguments[index + 1]) else {
                    return OffscreenRender.fail(usage)
                }
                size = parsed
                index += 1
            case "--example":
                guard index + 1 < arguments.count, let parsed = StarterExample.allCases.first(where: { $0.title.lowercased() == arguments[index + 1] || String(describing: $0) == arguments[index + 1] })
                else { return OffscreenRender.fail(usage) }
                example = parsed
                index += 1
            case "--add":
                guard index + 1 < arguments.count else { return OffscreenRender.fail(usage) }
                added.append(arguments[index + 1])
                index += 1
            case "--show-inspector":
                guard index + 1 < arguments.count else { return OffscreenRender.fail(usage) }
                inspector = arguments[index + 1] != "no"
                index += 1
            default:
                positional.append(arguments[index])
            }
            index += 1
        }
        guard positional.count == 2 || positional.count == 4 else { return OffscreenRender.fail(usage) }
        let width = positional.count == 4 ? Double(positional[2]) : 1440
        let height = positional.count == 4 ? Double(positional[3]) : 900
        guard let width, let height, width >= 200, height >= 200 else { return OffscreenRender.fail(usage) }

        // The choices of this run ride in the argument domain, which overrides the preferences
        // without writing to them.
        var overrides = UserDefaults.standard.volatileDomain(forName: UserDefaults.argumentDomain)
        overrides[Theme.defaultsKey] = themeOptions.theme.rawValue
        overrides[TimelineSetting.lowerPaneKey] = pane.rawValue
        overrides[InterfaceSize.defaultsKey] = size.rawValue
        overrides[InspectorSetting.defaultsKey] = inspector
        overrides[TimelineSetting.shownKey] = true
        UserDefaults.standard.setVolatileDomain(overrides, forName: UserDefaults.argumentDomain)

        do {
            let document = HelixDocument()
            try document.activate()
            var title = "Untitled"
            if positional[0] != SnapshotCommand.newSceneArgument {
                let url = URL(fileURLWithPath: positional[0])
                title = url.lastPathComponent
                if ImportFormats.canImport(url) {
                    if let message = document.importFile(url) { return OffscreenRender.fail(message) }
                } else {
                    try HelixError.check(helix.core.openScene(std.string(positional[0])))
                    document.cookGeometry()
                    document.refreshEditor(frame: true)
                }
            }
            if let example { document.addStarter(example) }
            // What the shelf does: a node of a type, under the selected node or in the middle.
            for name in added {
                guard let type = document.editor.paletteTypes.first(where: { $0.name == name || $0.name == "helix::" + name })
                else { return OffscreenRender.fail("no node type named \(name)") }
                document.editor.addNode(type)
            }
            if !selected.isEmpty {
                let wanted = Set(selected)
                let ids = document.editor.graph.nodes.filter { wanted.contains($0.name) }.map(\.id)
                if ids.isEmpty { return OffscreenRender.fail("no node named \(selected.joined(separator: ", "))") }
                document.editor.select(nodes: Set(ids))
            }

            let dark = themeOptions.appearanceDark ?? false
            guard let image = render(document, title: title, width: width, height: height, themeOptions: themeOptions, dark: dark)
            else { return OffscreenRender.fail("the window could not be drawn") }
            try OffscreenRender.write(image, to: positional[1])
            print("wrote \(positional[1]) (\(Int(width)) x \(Int(height)) points, \(image.width) x \(image.height) pixels)")
            return 0
        } catch {
            return OffscreenRender.fail(error.localizedDescription)
        }
    }

    private static func render(
        _ document: HelixDocument, title: String, width: Double, height: Double, themeOptions: ThemeOptions, dark: Bool
    ) -> CGImage? {
        let application = NSApplication.shared
        application.setActivationPolicy(.accessory)
        // Every view starts in the appearance asked for, the columns of the window included.
        let forcedFirst = themeOptions.theme.forcedDark ?? dark
        ThemeAppearance.override = NSAppearance(named: forcedFirst ? .darkAqua : .aqua)
        application.appearance = ThemeAppearance.override

        let still = ViewportStill()
        let content = DocumentView(document: document, fileURL: nil)
            .themed()
            .sized()
            .environment(\.viewportStill, still)
        let window = UnconstrainedWindow(
            contentRect: NSRect(x: 0, y: 0, width: width, height: height),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered, defer: false
        )
        window.contentViewController = NSHostingController(rootView: content)
        window.title = title
        window.setContentSize(NSSize(width: width, height: height))
        // The system theme follows the appearance asked for; a custom theme sets its own.
        let forced = themeOptions.theme.forcedDark ?? dark
        window.appearance = NSAppearance(named: forced ? .darkAqua : .aqua)
        window.setFrameOrigin(NSPoint(x: 40, y: 40))
        window.alphaValue = 0
        application.activate()
        window.makeKeyAndOrderFront(nil)
        settle(2.5)

        // The viewport: what the engine draws, at the size the layout gave it.
        if still.size.width > 1, still.size.height > 1 {
            still.image = viewportPicture(width: still.size.width, height: still.size.height, theme: themeOptions.theme, dark: forced)
        }
        settle(0.8)
        // What a column drew before the appearance settled is drawn again.
        window.appearance = NSAppearance(named: forced ? .darkAqua : .aqua)
        invalidate(window.contentView?.superview)
        window.displayIfNeeded()
        settle(0.5)

        guard let frame = window.contentView?.superview,
              let representation = frame.bitmapImageRepForCachingDisplay(in: frame.bounds)
        else { return nil }
        frame.cacheDisplay(in: frame.bounds, to: representation)
        window.orderOut(nil)
        return representation.cgImage
    }

    /// The geometry drawn by the engine, framed as the viewport frames it.
    private static func viewportPicture(width: Double, height: Double, theme: Theme, dark: Bool) -> NSImage? {
        let pixelsWide = UInt32(max(width * 2, 2)), pixelsHigh = UInt32(max(height * 2, 2))
        helix.core.resetViewport()
        _ = helix.core.frameViewport(Float(width / height))
        var options = helix.core.ViewportFrameOptions()
        options.pixelWidth = pixelsWide
        options.pixelHeight = pixelsHigh
        theme.palette(systemIsDark: dark).apply(to: &options)
        options.wireframe = WireframeSetting.current.facadeMode
        options.materialsVisible = MaterialsSetting.isVisible
        let path = FileManager.default.temporaryDirectory.appending(path: "helix-window-\(UUID().uuidString).png").path
        defer { try? FileManager.default.removeItem(atPath: path) }
        guard helix.core.renderToImage(std.string(path), options, true).ok else { return nil }
        return NSImage(contentsOfFile: path)
    }

    private static func invalidate(_ view: NSView?) {
        guard let view else { return }
        view.needsDisplay = true
        view.needsLayout = true
        for subview in view.subviews { invalidate(subview) }
    }

    private static func settle(_ seconds: TimeInterval) {
        let until = Date(timeIntervalSinceNow: seconds)
        while Date() < until { RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.05)) }
    }
}

/// A window the system does not move back inside the screen, so one larger than the display can
/// be laid out and read.
private final class UnconstrainedWindow: NSWindow {
    override func constrainFrameRect(_ frameRect: NSRect, to screen: NSScreen?) -> NSRect { frameRect }
}
