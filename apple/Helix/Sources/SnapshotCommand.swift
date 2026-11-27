import Foundation
import CxxStdlib
import HelixCore

/// `Helix --render-snapshot <in.kut | --new-scene> <out.png> [width height]`
///
/// Opens a scene, frames its geometry, draws it off screen and writes a PNG, with no window.
/// It goes through the same facade calls as the viewport, so a picture is a check that the
/// whole chain, from the file to the pixels, works.
@MainActor
enum SnapshotCommand {
    static let flag = "--render-snapshot"
    static let newSceneArgument = "--new-scene"

    static let usage = "usage: Helix \(flag) <in.kut | \(newSceneArgument)> <out.png> [width height]"

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

    private static func fail(_ message: String) -> Int32 {
        FileHandle.standardError.write(Data("Helix: \(message)\n".utf8))
        return 1
    }
}
