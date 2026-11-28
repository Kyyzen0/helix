import CxxStdlib
import Foundation
import HelixCore

/// A debug aid for `Scripts/launch-smoke.py`, off unless asked for in the environment.
extension HelixDocument {
    /// With `HELIX_DEBUG_ANIMATE_SKELETON=<path of a rigged file>`, imports the file, makes the pose
    /// and skin nodes of its skeleton for the clip `HELIX_DEBUG_ANIMATE_CLIP` the way the Animate
    /// button does, and says what the scene held ("helix-launch: animate ..."): the clips, the nodes
    /// (named in the language of the launch), the links, the display node, the clip set, whether
    /// the scene counts as animated for the timeline, and the same after one undo.
    func scheduleDebugAnimateCheck() {
        let environment = ProcessInfo.processInfo.environment
        guard let path = environment["HELIX_DEBUG_ANIMATE_SKELETON"], !path.isEmpty else { return }
        let clip = environment["HELIX_DEBUG_ANIMATE_CLIP"] ?? ""
        Task { @MainActor [weak self] in
            try? await Task.sleep(for: .seconds(3))
            guard let self, self.isActive else { return }
            let undo = self.undoManager
            undo?.groupsByEvent = false

            func names() -> [String] { helix.core.listNodes().map { String(engine: $0.name) }.sorted() }
            func links() -> [String] {
                let byID = Dictionary(uniqueKeysWithValues: helix.core.listNodes().map { ($0.id, String(engine: $0.name)) })
                return helix.core.listLinks().map {
                    "\(byID[$0.sourceNode] ?? "?").\($0.sourceOutput)>\(byID[$0.targetNode] ?? "?").\($0.targetInput)"
                }.sorted()
            }
            func display() -> String {
                helix.core.listNodes().first { $0.id == helix.core.displayNode() }.map { String(engine: $0.name) } ?? "none"
            }

            undo?.beginUndoGrouping()
            let error = self.importFile(URL(fileURLWithPath: path))
            undo?.endUndoGrouping()
            guard error == nil, let source = helix.core.listNodes().first else {
                Self.debugLog("animate import-error=\(error ?? "no node")") // l10n-ok: read by launch-smoke.py
                return
            }
            let before = (names().joined(separator: ","), self.sceneAnimates)
            let clips = (self.skeletonClips(ofImport: source.id) ?? []).map(\.name).joined(separator: ",")

            undo?.beginUndoGrouping()
            let outcome = self.animateSkeleton(importNode: source.id, clip: clip)
            undo?.endUndoGrouping()
            let after = names().joined(separator: ",")
            let poseClip = helix.core.listNodes().first { String(engine: $0.type) == SkeletonChain.poseType }.map {
                String(engine: helix.core.getString($0.id, "clip", 0).value)
            } ?? ""
            let linked = links().joined(separator: ";")
            let shown = display()
            let animates = self.sceneAnimates
            let selected = self.editor.selectedNodes.count

            undo?.undo()
            let undone = names().joined(separator: ",")
            let undoneAnimates = self.sceneAnimates
            Self.debugLog("animate clips=\(clips) before=\(before.0) before-animates=\(before.1) error=\(outcome.error ?? "none") after=\(after) links=\(linked) display=\(shown) clip=\(poseClip) animates=\(animates) selected=\(selected) undone=\(undone) undone-animates=\(undoneAnimates)") // l10n-ok: read by launch-smoke.py
        }
    }
}
