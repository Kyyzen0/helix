import CxxStdlib
import Foundation
import HelixCore

/// Animating a rigged import in one step.
extension HelixDocument {
    /// The clips of the skeleton of an import node, nil when the node has none.
    func skeletonClips(ofImport node: UInt64) -> [SkeletonClip]? {
        guard isActive else { return nil }
        return SkeletonChain.clips(ofImport: node)
    }

    /// Makes the pose node and the skin deform of an import, wires them, sets the clip and shows
    /// the skin: one step of undo, and the pose node (where the clip is picked) is selected. The
    /// two nodes are named from their labels in the language of the app, as the palette names a
    /// node, and go under the import where the palette would put them.
    func animateSkeleton(importNode node: UInt64, clip: String?) -> EditOutcome {
        guard isActive else { return .failed(String(localized: "This scene is closed.")) }
        guard let source = editor.graph.node(node) else {
            return .failed(String(localized: "This node has no skeleton to animate."))
        }
        let places = NodePlacement.skeletonChain(below: source.position, in: editor.graph)
        func firstName(_ type: String) -> String {
            let label = editor.paletteTypes.first { $0.name == type }?.label ?? ""
            let first = NodeNaming.firstName(forLabel: label)
            return first.isEmpty ? "" : String(engine: helix.core.freeNodeName(std.string(first)))
        }
        let names = (pose: firstName(SkeletonChain.poseType), deform: firstName(SkeletonChain.deformType))
        let outcome = performEdit(named: String(localized: "Animate Skeleton")) {
            switch SkeletonChain.build(
                importNode: node, clip: clip, names: names, poseAt: places.pose, deformAt: places.deform
            ) {
            case .success(let built): return EditOutcome(error: nil, createdNode: built.pose)
            case .failure(let failure): return .failed(failure.localizedMessage)
            }
        }
        if let pose = outcome.createdNode {
            editor.select(nodes: [pose])
            editor.reveal(pose)
        }
        return outcome
    }
}
