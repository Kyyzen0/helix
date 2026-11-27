import Combine
import CxxStdlib
import Foundation
import HelixCore

/// The edits of the node editor, each one a step of undo.
///
/// Undo keeps the scene as the engine writes it. An edit captures the scene before and after it
/// and registers, on the undo manager of the window, a step that puts the one back for the other.
/// That is the same text a saved file holds, so whatever a file keeps (the nodes, their
/// parameters, the links, the display node, the keyframes and the time) comes back, and any new
/// kind of edit is undoable without code of its own. A restore gives the nodes new ids, so the
/// editor finds its selection again by path.
extension HelixDocument: NodeEditorEditing {
    /// Reads the graph again, for the editor to show.
    func refreshEditor(frame: Bool = false) {
        editor.reload(EditorGraphLoader.current(), frame: frame)
        // What the inspector shows (the values, what a condition hides, the errors a cook just
        // found) can have changed with the graph.
        inspector.reload()
        // So can the keys on the ruler, the range, and what the table follows and shows.
        timeline.reload()
        followedNodeChanged(selection: editor.selectedNodes)
    }

    /// Makes the editor and the inspector ready, once the scene is loaded.
    func prepareEditor() {
        editor.editing = self
        editor.paletteTypes = NodeTypeCatalog.all
        inspector.editing = self
        // The inspector shows the node the editor has selected. The publisher fires before the
        // selection is set, so the new selection comes with it.
        inspectorSubscription = editor.$selectedNodes.sink { [weak self] selection in
            self?.inspector.select(nodes: selection)
            self?.followedNodeChanged(selection: selection)
        }
        refreshEditor(frame: true)
    }

    // MARK: Undo

    /// Runs an edit and, when it changed the scene, registers its undo step. Returns what the
    /// edit came to, with the engine's reason when it was refused.
    @discardableResult
    func performEdit(
        named name: String, recook: Bool = true, reframe: Bool = true, _ body: () -> EditOutcome
    ) -> EditOutcome {
        guard isActive else { return .failed(String(localized: "This scene is closed.")) }
        // An edit made while playing stops the playback: the frame that goes on moving would make
        // an edit land on another frame than the one the person was looking at, and a key set by
        // the edit would be set on a frame that was not the one on screen.
        timeline.pause()
        // An edit made while a drag goes on is part of the drag's step, which captures the
        // scene itself when it ends.
        if continuousEdit.isActive {
            let outcome = body()
            if recook { cookGeometry(reframe: reframe) }
            refreshEditor()
            return outcome
        }
        let before = helix.core.captureScene()
        // An edit that cannot be undone is not made.
        guard before.status.ok else { return .failed(String(engine: before.status.error)) }

        let outcome = body()

        let after = helix.core.captureScene()
        let beforeText = String(engineBytes: before.data)
        let afterText = after.status.ok ? String(engineBytes: after.data) : nil
        let changed = afterText != beforeText
        if let afterText, changed {
            registerUndo(named: name, restoring: beforeText, otherwise: afterText)
        }
        if changed, recook { cookGeometry(reframe: reframe) }
        refreshEditor()
        return outcome
    }

    /// Puts a step on the undo manager that restores `snapshot`. The step, in turn, registers
    /// the way back to `otherwise`, which is how redo comes about.
    func registerUndo(named name: String, restoring snapshot: String, otherwise: String) {
        guard let undoManager else { return }
        undoManager.registerUndo(withTarget: self) { document in
            MainActor.assumeIsolated {
                document.restore(snapshot, otherwise: otherwise, named: name)
            }
        }
        undoManager.setActionName(name)
    }

    private func restore(_ snapshot: String, otherwise: String, named name: String) {
        let selectedPaths = selectedPathsBeforeRestore()
        timeline.pause()
        var status = helix.core.Status()
        // The scene holds the frame it was captured on, and the playhead is not part of an edit.
        keepingPlayhead { status = helix.core.restoreScene(std.string(snapshot)) }
        if !status.ok { presentedError = String(engine: status.error) }
        registerUndo(named: name, restoring: otherwise, otherwise: snapshot)
        cookGeometry(reframe: false)
        reselectAfterRestore(paths: selectedPaths)
    }

    /// The paths of the selected nodes. A restore gives the nodes new ids, so the selection is
    /// kept by path.
    func selectedPathsBeforeRestore() -> Set<String> {
        Set(editor.graph.nodes.filter { editor.selectedNodes.contains($0.id) }.map(\.path))
    }

    /// Reads the graph again after a restore and selects what was selected before it.
    func reselectAfterRestore(paths: Set<String>? = nil) {
        let selected = paths ?? selectedPathsBeforeRestore()
        refreshEditor()
        editor.select(nodes: Set(editor.graph.nodes.filter { selected.contains($0.path) }.map(\.id)))
    }

    private func failure(_ status: helix.core.Status) -> EditOutcome {
        .failed(String(engine: status.error))
    }

    private func outcome(_ status: helix.core.Status) -> EditOutcome {
        status.ok ? .done : failure(status)
    }

    // MARK: Edits

    func moveNodes(_ moves: [NodeMove]) -> EditOutcome {
        let name = moves.count == 1 ? String(localized: "Move Node") : String(localized: "Move Nodes")
        // Moving a node changes nothing the viewport shows.
        return performEdit(named: name, recook: false) {
            for move in moves {
                let status = helix.core.moveNode(move.id, Float(move.position.x), Float(move.position.y))
                if !status.ok { return failure(status) }
            }
            return .done
        }
    }

    func connect(_ link: EditorLink) -> EditOutcome {
        performEdit(named: String(localized: "Connect")) { outcome(wire(link)) }
    }

    func reconnect(_ old: EditorLink, to new: EditorLink) -> EditOutcome {
        performEdit(named: String(localized: "Connect")) {
            let removed = unwire(old)
            if !removed.ok { return failure(removed) }
            let status = wire(new)
            if !status.ok {
                // The link goes back where it was, so a refused drop changes nothing.
                _ = wire(old)
                return failure(status)
            }
            return .done
        }
    }

    func disconnect(_ link: EditorLink) -> EditOutcome {
        performEdit(named: String(localized: "Disconnect")) {
            outcome(unwire(link))
        }
    }

    func delete(nodes: [UInt64], links: [EditorLink]) -> EditOutcome {
        let name: String
        if nodes.isEmpty {
            name = links.count == 1 ? String(localized: "Delete Link") : String(localized: "Delete Links")
        } else {
            name = nodes.count == 1 ? String(localized: "Delete Node") : String(localized: "Delete Nodes")
        }
        return performEdit(named: name) {
            for link in EditorLink.removalOrder(links) {
                let status = unwire(link)
                if !status.ok { return failure(status) }
            }
            for node in nodes {
                let status = helix.core.deleteNode(node)
                if !status.ok { return failure(status) }
            }
            return .done
        }
    }

    func create(_ type: EditorNodeType, at position: CGPoint, wiredFrom: (node: UInt64, output: Int)?) -> EditOutcome {
        performEdit(named: String(localized: "Add \(type.label)")) {
            let result = helix.core.createNode(std.string(type.name), std.string(""), Float(position.x), Float(position.y))
            if !result.status.ok { return failure(result.status) }
            if let wiredFrom {
                let link = EditorLink(sourceNode: wiredFrom.node, sourceOutput: wiredFrom.output, targetNode: result.id, targetInput: 0)
                let status = wire(link)
                if !status.ok {
                    _ = helix.core.deleteNode(result.id)
                    return failure(status)
                }
            }
            // A scene that shows nothing yet shows the first node made, so the viewport is not
            // left empty after the first step.
            if helix.core.displayNode() == 0 { _ = helix.core.setDisplayNode(result.id) }
            return EditOutcome(error: nil, createdNode: result.id)
        }
    }

    func setDisplay(_ node: UInt64) -> EditOutcome {
        guard helix.core.displayNode() != node else { return .done }
        return performEdit(named: String(localized: "Set Display Node")) {
            outcome(helix.core.setDisplayNode(node))
        }
    }

    private func wire(_ link: EditorLink) -> helix.core.Status {
        helix.core.connectNodes(link.sourceNode, UInt32(link.sourceOutput), link.targetNode, UInt32(link.targetInput))
    }

    private func unwire(_ link: EditorLink) -> helix.core.Status {
        helix.core.disconnectNodes(link.sourceNode, UInt32(link.sourceOutput), link.targetNode, UInt32(link.targetInput))
    }
}
