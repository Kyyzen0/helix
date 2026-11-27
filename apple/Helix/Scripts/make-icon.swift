// Fills the application icon of the native app from the Helix logo in assets/logo, so the logo
// has one source for the Qt bundle (assets/logo/Helix.icns) and for this app. Run it after the
// logo changes:
//
//   swift apple/Helix/Scripts/make-icon.swift
import Foundation

let root = URL(fileURLWithPath: CommandLine.arguments[0])
    .deletingLastPathComponent().deletingLastPathComponent()
    .deletingLastPathComponent().deletingLastPathComponent()
let iconset = root.appending(path: "assets/logo/Helix.iconset")
let output = root.appending(path: "apple/Helix/Resources/Assets.xcassets/AppIcon.appiconset")

// The iconset names a 2x image icon_NxN@2x.png and a 1x one icon_NxN.png; the asset catalog of the
// app names the 1x one icon_NxN@1x.png.
let fileManager = FileManager.default
for size in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let source = iconset.appending(path: scale == 1 ? "icon_\(size)x\(size).png" : "icon_\(size)x\(size)@2x.png")
        let target = output.appending(path: "icon_\(size)x\(size)@\(scale)x.png")
        do {
            if fileManager.fileExists(atPath: target.path) { try fileManager.removeItem(at: target) }
            try fileManager.copyItem(at: source, to: target)
        } catch {
            FileHandle.standardError.write(Data("Couldn't copy \(source.path): \(error.localizedDescription)\n".utf8))
            exit(1)
        }
    }
}
print("Wrote the app icon from \(iconset.path)")
