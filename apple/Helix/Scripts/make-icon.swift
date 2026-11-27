// Draws the application icon: the white Helix mark on a dark rounded square, at every size an
// macOS app icon needs. Run it after changing the mark:
//
//   swift apple/Helix/Scripts/make-icon.swift
import AppKit

let root = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
let logoURL = root.appending(path: "static/icons/icon-main-white.png")
let outputFolder = root.appending(path: "apple/Helix/Resources/Assets.xcassets/AppIcon.appiconset")

guard let logo = NSImage(contentsOf: logoURL) else {
    FileHandle.standardError.write(Data("Couldn't read \(logoURL.path)\n".utf8))
    exit(1)
}

func render(pixels: Int) -> Data {
    let size = CGFloat(pixels)
    let bitmap = NSBitmapImageRep(
        bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels, bitsPerSample: 8,
        samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
        bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)

    // The icon grid of macOS: the square takes 824 of 1024 points, with room for the shadow.
    let inset = size * 100 / 1024
    let square = NSRect(x: inset, y: inset, width: size - 2 * inset, height: size - 2 * inset)
    let path = NSBezierPath(roundedRect: square, xRadius: square.width * 0.225, yRadius: square.width * 0.225)
    NSGradient(colors: [NSColor(srgbRed: 0.17, green: 0.18, blue: 0.21, alpha: 1),
                        NSColor(srgbRed: 0.08, green: 0.09, blue: 0.11, alpha: 1)])!
        .draw(in: path, angle: -90)

    // The mark, centred, in half the width of the square.
    let markWidth = square.width * 0.5
    let markHeight = markWidth * logo.size.height / logo.size.width
    logo.draw(
        in: NSRect(x: square.midX - markWidth / 2, y: square.midY - markHeight / 2, width: markWidth, height: markHeight),
        from: .zero, operation: .sourceOver, fraction: 1)

    NSGraphicsContext.restoreGraphicsState()
    return bitmap.representation(using: .png, properties: [:])!
}

var images: [[String: String]] = []
for points in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let name = "icon_\(points)x\(points)@\(scale)x.png"
        try render(pixels: points * scale).write(to: outputFolder.appending(path: name))
        images.append(["size": "\(points)x\(points)", "idiom": "mac", "filename": name, "scale": "\(scale)x"])
    }
}
let contents: [String: Any] = ["images": images, "info": ["version": 1, "author": "xcode"]]
try JSONSerialization.data(withJSONObject: contents, options: [.prettyPrinted, .sortedKeys])
    .write(to: outputFolder.appending(path: "Contents.json"))
print("wrote \(images.count) icons")
