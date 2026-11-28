import CxxStdlib
import Foundation
import HelixCore

/// The tools of the server that animate a rigged import and take geometry out of the scene.
extension MCPHandlers {
    /// What a chain of nodes looks like to an agent: the ids and the clips the skeleton has.
    static func animateSkeleton(_ arguments: Arguments) throws -> MCPToolOutput {
        let source = try resolve(arguments.values["node"])
        let clip = try arguments.string("clip")
        let id = source.id
        let position = CGPoint(x: CGFloat(source.x), y: CGFloat(source.y))
        // The layout of the app, below the import: the pose a card to the right.
        let row = EditorLayout.chainSpacing
        let built = SkeletonChain.build(
            importNode: id, clip: clip, names: ("", ""),
            poseAt: CGPoint(x: position.x + EditorLayout.cardSize.width + 12, y: position.y + row),
            deformAt: CGPoint(x: position.x, y: position.y + 2 * row)
        )
        switch built {
        case .failure(let failure):
            throw MCPToolError(failure.englishMessage)
        case .success(let chain):
            let clips = SkeletonChain.clips(ofImport: id) ?? []
            return .data([
                "import": identity(source),
                "pose": describe(chain.pose),
                "deform": describe(chain.deform),
                "clip": .string(chain.clip.isEmpty ? (clips.first?.name ?? "") : chain.clip),
                "clips": .array(clips.map { ["name": .string($0.name), "duration": .init(Float($0.duration))] }),
                "display_node": describe(chain.deform),
            ])
        }
    }

    static func exportGeometry(_ arguments: Arguments) throws -> MCPToolOutput {
        let path = expanded(try arguments.requiredString("path"))
        guard (path as NSString).pathExtension.lowercased() == "obj" else {
            throw MCPToolError("Helix writes Wavefront obj files: the path must end in .obj.") // l10n-ok: for an agent, in English
        }
        let result = helix.core.exportGeometry(std.string(path))
        try check(result.status)
        return .data([
            "path": .string(path),
            "format": "obj",
            "point_count": .init(count: result.pointCount),
            "face_count": .init(count: result.faceCount),
            "line_count": .init(count: result.lineCount),
        ])
    }
}
