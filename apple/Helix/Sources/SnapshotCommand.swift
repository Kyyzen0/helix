import Foundation
import CxxStdlib
import HelixCore

/// `Helix --render-snapshot <in.kut | 3D file | --new-scene> <out.png> [width height]`
///
/// Opens a scene (or imports a 3D file into a new one), frames its geometry, draws it off screen
/// and writes a PNG, with no window. Prints how long the import took and how much memory the
/// process used at most.
/// It goes through the same facade calls as the viewport, so a picture is a check that the
/// whole chain, from the file to the pixels, works.
@MainActor
enum SnapshotCommand {
    static let flag = "--render-snapshot"
    static let newSceneArgument = "--new-scene"

    static let usage = "usage: Helix \(flag) <in.kut | 3D file | \(newSceneArgument)> <out.png> [width height]"

    /// Runs the command on its arguments, the ones after the flag, and returns the exit code.
    static func run(_ arguments: [String]) -> Int32 {
        guard arguments.count == 2 || arguments.count == 4 else { return fail(usage) }
        let width = arguments.count == 4 ? UInt32(arguments[2]) : 1024
        let height = arguments.count == 4 ? UInt32(arguments[3]) : 768
        guard let width, let height, width > 0, height > 0 else { return fail(usage) }

        do {
            try EngineHost.shared.startEngine()
            if arguments[0] == newSceneArgument {
                try EngineHost.shared.makeDefaultScene()
            } else if ImportFormats.canImport(URL(fileURLWithPath: arguments[0])) {
                try importScene(arguments[0])
            } else {
                try HelixError.check(helix.core.openScene(std.string(arguments[0])))
            }

            let geometry = helix.core.updateViewportGeometry()
            try HelixError.check(geometry.status)
            print("\(geometry.pointCount) points, \(geometry.triangleCount) triangles")

            // The same framing the window does on its first frame.
            helix.core.resetViewport()
            _ = helix.core.frameViewport(Float(width) / Float(height))

            var options = helix.core.ViewportFrameOptions()
            options.pixelWidth = width
            options.pixelHeight = height
            options.backgroundRed = ViewportRenderer.background.red
            options.backgroundGreen = ViewportRenderer.background.green
            options.backgroundBlue = ViewportRenderer.background.blue
            try HelixError.check(helix.core.renderToImage(std.string(arguments[1]), options, true))
            print("wrote \(arguments[1]) (\(width) x \(height))")
            return 0
        } catch {
            return fail(error.localizedDescription)
        }
    }

    /// A new scene that imports a 3D file and shows it, as opening the file in the app does.
    private static func importScene(_ path: String) throws {
        try HelixError.check(helix.core.newScene())
        let started = DispatchTime.now()
        let result = helix.core.importFile(std.string(path))
        try HelixError.check(result.status)
        try HelixError.check(helix.core.setDisplayNode(result.id))
        // The cook is where the geometry is built, so it is part of what the import costs.
        let summary = helix.core.cookDisplaySummary()
        try HelixError.check(summary.status)
        let seconds = Double(DispatchTime.now().uptimeNanoseconds - started.uptimeNanoseconds) / 1e9
        print("imported in \(String(format: "%.2f", seconds)) s, peak memory \(peakMemoryMegabytes()) MB")
        print(String(engine: helix.core.lastImportSummary()))
    }

    /// The most memory the process has held, in megabytes.
    private static func peakMemoryMegabytes() -> Int {
        var usage = rusage()
        getrusage(RUSAGE_SELF, &usage)
        // ru_maxrss is in bytes on macOS.
        return Int(usage.ru_maxrss) / (1024 * 1024)
    }

    private static func fail(_ message: String) -> Int32 {
        FileHandle.standardError.write(Data("Helix: \(message)\n".utf8))
        return 1
    }
}
