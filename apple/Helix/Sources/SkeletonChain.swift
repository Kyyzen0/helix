import CoreGraphics
import CxxStdlib
import Foundation
import HelixCore

/// The skeleton chain of an import in one call: a pose node on its skeleton output and a skin
/// deform on its geometry output and on that pose, the clip set, the deform displayed.
///
/// This is the engine half, shared by the interface (one step of undo around it) and the MCP tool
/// of the same name. It writes to the scene and registers nothing: the caller brackets it.
@MainActor
enum SkeletonChain {
    struct Built {
        var pose: UInt64
        var deform: UInt64
        /// The clip the pose plays, empty for the first one the skeleton has.
        var clip: String
    }

    /// Why a chain was not made. The interface says it in the language of the app, an agent in
    /// English.
    enum Failure: Error, Equatable {
        case noSkeleton
        case unknownClip(String, known: [String])
        case notMade(typeName: String)
        /// The raw (English) sentence of the engine.
        case engine(String)

        var englishMessage: String {
            switch self {
            case .noSkeleton: "The node has no skeleton to animate: only an import whose file holds one does." // l10n-ok: for an agent
            case .unknownClip(let name, let known):
                "The skeleton has no clip named '\(name)'. Its clips are: \(known.joined(separator: ", "))." // l10n-ok: for an agent
            case .notMade(let typeName): "The \(typeName) node could not be made." // l10n-ok: for an agent
            case .engine(let message): message
            }
        }

        var localizedMessage: String {
            switch self {
            case .noSkeleton: String(localized: "This node has no skeleton to animate.")
            case .unknownClip(let name, let known):
                String(localized: "The skeleton has no clip named “\(name)”. Its clips are: \(known.joined(separator: ", ")).")
            case .notMade: String(localized: "The nodes that animate the skeleton could not be made.")
            case .engine(let message): String(engineMessage: std.string(message))
            }
        }
    }

    /// The type names the chain is made of, and the output of an import that holds the skeleton.
    static let importType = "helix::geometryImport"
    static let poseType = "helix::skeletonPose"
    static let deformType = "helix::skinDeform"
    static let skeletonOutput: UInt32 = 1

    /// The clips of the skeleton an import node produces, or nil when the node is not an import or
    /// its file has no skeleton. Cooks the node when it is dirty.
    static func clips(ofImport node: UInt64) -> [SkeletonClip]? {
        guard helix.core.listNodes().first(where: { $0.id == node }).map({ String(engine: $0.type) }) == importType
        else { return nil }
        let result = helix.core.skeletonClipsOfOutput(node, skeletonOutput)
        guard result.status.ok, result.present else { return nil }
        return result.clips.map { SkeletonClip(name: String(engine: $0.name), duration: Double($0.duration)) }
    }

    /// Makes the chain. `names` are asked of the engine as they are (empty lets it name the node),
    /// `clip` is checked against the clips of the skeleton before anything is made, and a failure
    /// after that takes back what was made, so a refusal leaves the scene as it was.
    static func build(
        importNode: UInt64, clip: String?, names: (pose: String, deform: String),
        poseAt: CGPoint, deformAt: CGPoint
    ) -> Result<Built, Failure> {
        guard let clips = clips(ofImport: importNode) else {
            return .failure(.noSkeleton)
        }
        let wanted = clip ?? ""
        if !wanted.isEmpty, !clips.contains(where: { $0.name == wanted }) {
            return .failure(.unknownClip(wanted, known: clips.map(\.name)))
        }

        var made: [UInt64] = []
        func undoWork(_ failure: Failure) -> Result<Built, Failure> {
            for node in made.reversed() { _ = helix.core.deleteNode(node) }
            return .failure(failure)
        }
        func make(_ type: String, _ name: String, at point: CGPoint) -> UInt64? {
            let result = helix.core.createNode(std.string(type), std.string(name), Float(point.x), Float(point.y))
            guard result.status.ok else { return nil }
            made.append(result.id)
            return result.id
        }

        guard let pose = make(poseType, names.pose, at: poseAt) else {
            return undoWork(.notMade(typeName: poseType))
        }
        guard let deform = make(deformType, names.deform, at: deformAt) else {
            return undoWork(.notMade(typeName: deformType))
        }
        // The skeleton goes to the pose, the geometry and the pose to the skin.
        let links: [(UInt64, UInt32, UInt64, UInt32)] = [
            (importNode, skeletonOutput, pose, 0), (importNode, 0, deform, 0), (pose, 0, deform, 1),
        ]
        for link in links {
            let status = helix.core.connectNodes(link.0, link.1, link.2, link.3)
            if !status.ok { return undoWork(.engine(String(engine: status.error))) }
        }
        if !wanted.isEmpty {
            let status = helix.core.setString(pose, std.string("clip"), 0, std.string(wanted))
            if !status.ok { return undoWork(.engine(String(engine: status.error))) }
        }
        let display = helix.core.setDisplayNode(deform)
        if !display.ok { return undoWork(.engine(String(engine: display.error))) }
        return .success(Built(pose: pose, deform: deform, clip: wanted))
    }
}
