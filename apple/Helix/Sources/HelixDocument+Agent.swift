import AppKit
import CxxStdlib
import Foundation
import HelixCore
import UniformTypeIdentifiers

/// What an AI agent's call does to the open scene, in the way of the person's own edits.
extension HelixDocument {
    /// Runs the changes of one tool call as ONE step of undo, named for the window's Edit menu, and
    /// shows them at once: the editor, the inspector, the timeline and the viewport read the scene
    /// again, as they do after an edit of the person's.
    ///
    /// `performEdit` keeps what a refused edit did before it failed, which suits the person, who
    /// sees it. An agent's call that fails must leave nothing: a tool that wrote three components
    /// of a value and was refused on the fourth, or made a node and failed to wire it. So when the
    /// body throws, the scene goes back to what it was before the call, and no step is registered.
    /// The one exception to the person's rule is deliberate: an edit of the person's own cannot be
    /// half done, and a call of an agent can.
    ///
    /// Throws `MCPToolError`: its message is what the agent reads.
    func performToolEdit<T>(named name: String, recook: Bool = true, _ body: () throws -> T) throws -> T {
        guard isActive else { throw MCPToolError(MCPLive.noSceneMessage) }
        // A drag of the person's is going on and owns the scene until it ends: its step captures
        // the scene itself when it does.
        guard !continuousEdit.isActive else {
            throw MCPToolError("The person is dragging a value in Helix right now. Try again in a moment.") // l10n-ok: for an agent, in English
        }
        timeline.pause()
        let before = helix.core.captureScene()
        guard before.status.ok else { throw MCPToolError(String(engine: before.status.error)) }
        let beforeText = String(engineBytes: before.data)

        let result: T
        do {
            result = try body()
        } catch {
            let after = helix.core.captureScene()
            if !after.status.ok || String(engineBytes: after.data) != beforeText {
                keepingPlayhead { _ = helix.core.restoreScene(std.string(beforeText)) }
                cookGeometry(reframe: false)
                refreshEditor()
            }
            throw error
        }

        let after = helix.core.captureScene()
        let afterText = after.status.ok ? String(engineBytes: after.data) : nil
        if let afterText, afterText != beforeText {
            registerStepOfItsOwn(named: name, restoring: beforeText, otherwise: afterText)
            AgentLink.shared.countStep()
            if recook { cookGeometry(reframe: false) }
        }
        refreshEditor()
        return result
    }

    /// Registers the step in a group of its own. The undo manager of a window groups what is
    /// registered in one pass of the run loop, which is one action of the person; the calls of an
    /// agent come from a socket, in passes the manager does not tell apart, and two calls would be
    /// one Undo. So whatever group is open is closed first, and the step is closed after itself.
    private func registerStepOfItsOwn(named name: String, restoring snapshot: String, otherwise: String) {
        guard let manager = undoManager else { return }
        while manager.groupingLevel > 0 { manager.endUndoGrouping() }
        manager.beginUndoGrouping()
        registerUndo(named: name, restoring: snapshot, otherwise: otherwise)
        manager.endUndoGrouping()
    }

    /// The scene is on screen at a frame the agent asked for: the viewport, the inspector and the
    /// table follow, and the playhead moves. No step of undo: the playhead is not part of an edit.
    func showFrameSetByAgent() {
        timeline.pause()
        frameChanged()
        timeline.reload()
    }

    /// Saves the scene the way the person's Save does: to its file, or to a new one when it has none
    /// or the agent names another. The document then stands for that file.
    func saveForAgent(to path: String?) async throws -> URL {
        guard let system = systemDocument else { throw MCPToolError("The window of the scene is not ready. Try again in a moment.") } // l10n-ok: for an agent, in English
        let type = UTType.helixScene.identifier
        if let path, !path.isEmpty {
            let url = URL(fileURLWithPath: path)
            guard url.pathExtension.lowercased() == "kut" else {
                throw MCPToolError("A scene is saved as a .kut file: the path must end in .kut.") // l10n-ok: for an agent, in English
            }
            let sameFile = system.fileURL?.standardizedFileURL == url.standardizedFileURL
            do {
                try await system.save(to: url, ofType: type, for: sameFile ? .saveOperation : .saveAsOperation)
            } catch {
                throw MCPToolError("The scene could not be saved: \(error.localizedDescription)") // l10n-ok: for an agent, in English
            }
            return url
        }
        guard let url = system.fileURL else {
            throw MCPToolError("This scene has never been saved: give a path ending in .kut.") // l10n-ok: for an agent, in English
        }
        do {
            try await system.save(to: url, ofType: type, for: .saveOperation)
        } catch {
            throw MCPToolError("The scene could not be saved: \(error.localizedDescription)") // l10n-ok: for an agent, in English
        }
        return url
    }
}
