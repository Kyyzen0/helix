import CoreGraphics
import Foundation

/// The commands of the editor on the nodes as a whole: renaming, and copying and pasting them.
/// Each is one step of undo, made by the document (`NodeEditorEditing`).
extension NodeEditorModel {
    // MARK: Rename

    /// The one node a command on "the node" is about: the selected one, when exactly one is.
    var soleSelectedNode: UInt64? {
        selectedNodes.count == 1 ? selectedNodes.first : nil
    }

    /// Moves the selection to the card nearest in a direction, or adds it to the selection. Returns
    /// whether there was a card to go to, so a key with nowhere to go is left to the next responder.
    @discardableResult
    func moveSelection(_ direction: NodeNavigation.Direction, extending: Bool = false) -> Bool {
        guard renaming == nil, palette == nil,
              let target = NodeNavigation.target(
                  from: selectedNodes, toward: direction, in: graph.nodes, display: graph.displayNode
              )
        else { return false }
        select(nodes: extending ? selectedNodes.union([target]) : [target])
        reveal(target)
        return true
    }

    /// Starts typing a new name for a node, over its card. The node is the selected one when none
    /// is given.
    func beginRename(_ id: UInt64? = nil) {
        guard let id = id ?? soleSelectedNode, let node = graph.node(id) else { return }
        closePalette()
        reveal(id)
        renaming = RenameSession(node: id, text: node.name)
    }

    /// Takes the name that was typed. A name the engine refuses (another node has it) is shown
    /// beside the field and typing goes on; any other ends the rename.
    func commitRename() {
        guard var session = renaming else { return }
        let name = session.text.trimmingCharacters(in: .whitespaces)
        if name == graph.node(session.node)?.name {
            renaming = nil
            return
        }
        if let problem = NodeNaming.problem(with: name) {
            session.error = problem
            renaming = session
            return
        }
        let outcome = editing?.rename(session.node, to: name) ?? .done
        if let error = outcome.error {
            session.error = error
            renaming = session
        } else {
            renaming = nil
        }
    }

    func cancelRename() {
        renaming = nil
    }

    /// Takes the typing back to the field after a change of the text, so the last refusal is not
    /// left beside a name that is not the one it was about.
    func renameTextChanged(_ text: String) {
        guard var session = renaming else { return }
        session.text = text
        session.error = nil
        renaming = session
    }

    // MARK: Tidy

    /// Whether there is anything to tidy: a tidy of one node moves nothing.
    var canTidy: Bool { graph.nodes.count >= 2 }

    /// Puts the nodes in rows from the sources down, as one step of undo, then shows them. It
    /// tidies the selected nodes when two or more are selected, and the whole graph otherwise.
    func tidyGraph() {
        guard canTidy else { return }
        let part = selectedNodes.count >= 2 ? selectedNodes : nil
        let moves = GraphTidy.moves(for: graph, among: part)
        if !moves.isEmpty { reportOutcome(editing?.moveNodes(moves) ?? .done) }
        if part == nil { frameAll() } else { frameSelection() }
    }

    // MARK: Copy and paste

    /// Whether the pasteboard holds nodes to paste.
    var canPaste: Bool { editing?.canPaste ?? false }

    /// The nodes the copy commands are about: the selected ones, in the order of the graph.
    private var selectedIDs: [UInt64] {
        graph.nodes.map(\.id).filter { selectedNodes.contains($0) }
    }

    func copySelection() {
        let nodes = selectedIDs
        guard !nodes.isEmpty else { return }
        reportOutcome(editing?.copy(nodes: nodes) ?? .done)
    }

    func cutSelection() {
        let nodes = selectedIDs
        guard !nodes.isEmpty else { return }
        reportOutcome(editing?.cut(nodes: nodes) ?? .done)
    }

    /// Pastes under the pointer when it is over the editor, and next to the originals when not.
    func pasteFromPasteboard() {
        let target = pointerInside ? pointer.map(transform.canvasPoint) : nil
        selectMade(by: editing?.paste(at: target) ?? .done)
    }

    func duplicateSelection() {
        let nodes = selectedIDs
        guard !nodes.isEmpty else { return }
        selectMade(by: editing?.duplicate(nodes: nodes) ?? .done)
    }

    /// Makes the nodes an edit created the selection, and shows them.
    private func selectMade(by outcome: EditOutcome) {
        reportOutcome(outcome)
        guard outcome.error == nil, !outcome.createdNodes.isEmpty else { return }
        select(nodes: Set(outcome.createdNodes))
        if let first = outcome.createdNodes.first { reveal(first) }
    }

    func reportOutcome(_ outcome: EditOutcome) {
        if let error = outcome.error { report(error) }
    }
}
