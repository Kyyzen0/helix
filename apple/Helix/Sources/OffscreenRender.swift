import AppKit
import ImageIO
import SwiftUI
import UniformTypeIdentifiers

/// Draws a view in a window that is never shown, and reads its pixels back. The controls of the
/// interface are AppKit's (fields, tables, menus), which `ImageRenderer` draws as placeholders, so
/// a picture of them has to come from a real window.
@MainActor
enum OffscreenRender {
    /// The view drawn at a size. A height of nil gives the height the view asks for at that width.
    static func image<Content: View>(of content: Content, width: Double, height: Double?, dark: Bool) -> CGImage? {
        // The app has not started, but AppKit needs to exist to draw controls and symbols.
        _ = NSApplication.shared
        let host = NSHostingView(rootView: content)
        host.frame = NSRect(x: 0, y: 0, width: width, height: height ?? 100)
        let window = NSWindow(contentRect: host.frame, styleMask: [.borderless], backing: .buffered, defer: false)
        window.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
        window.contentView = host
        // Fields and tables finish setting themselves up on the run loop, as they do on screen.
        for _ in 0..<4 {
            host.layoutSubtreeIfNeeded()
            RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.15))
        }
        let natural = NSHostingController(rootView: content).sizeThatFits(in: CGSize(width: width, height: 20_000)).height
        let size = NSSize(width: width, height: height ?? ceil(natural))
        window.setContentSize(size)
        host.frame = NSRect(origin: .zero, size: size)
        for _ in 0..<3 {
            host.layoutSubtreeIfNeeded()
            RunLoop.current.run(until: Date(timeIntervalSinceNow: 0.2))
        }
        guard let representation = host.bitmapImageRepForCachingDisplay(in: host.bounds) else { return nil }
        host.cacheDisplay(in: host.bounds, to: representation)
        return representation.cgImage
    }

    static func write(_ image: CGImage, to path: String) throws {
        let url = URL(fileURLWithPath: path)
        guard let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil) else {
            throw HelixError.engine("Couldn't write \(path).")
        }
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination) else { throw HelixError.engine("Couldn't write \(path).") }
    }

    static func fail(_ message: String) -> Int32 {
        FileHandle.standardError.write(Data("Helix: \(message)\n".utf8))
        return 1
    }
}
