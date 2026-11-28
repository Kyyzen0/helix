import AppKit
import CxxStdlib
import Foundation
import HelixCore

/// The pasteboard of the node editor: the copy of nodes the engine makes, under a type of Helix's
/// own and as plain text.
enum NodePasteboard {
    static let type = NSPasteboard.PasteboardType(NodeClipboard.typeIdentifier)

    static func write(_ text: String, to board: NSPasteboard = .general) {
        board.clearContents()
        board.declareTypes([type, .string], owner: nil)
        board.setString(text, forType: type)
        // A copy kept as text can be pasted back into Helix from a note, so the plain text is
        // the copy itself.
        board.setString(text, forType: .string)
    }

    /// The copy on the pasteboard, or nil when it holds none.
    static func read(from board: NSPasteboard = .general) -> String? {
        if let text = board.string(forType: type) { return text }
        if let text = board.string(forType: .string), NodeClipboard.looksLikeCopy(text) { return text }
        return nil
    }

    static func hasNodes(in board: NSPasteboard = .general) -> Bool {
        guard let available = board.availableType(from: [type, .string]) else { return false }
        return available == type || read(from: board) != nil
    }
}

/// Renaming, copying, cutting, pasting and duplicating nodes, each one step of undo.
extension HelixDocument {
    private static func idList(_ nodes: [UInt64]) -> helix.core.NodeIdList {
        var list = helix.core.NodeIdList()
        for node in nodes { list.push_back(node) }
        return list
    }

    var canPaste: Bool { isActive && NodePasteboard.hasNodes() }

    // MARK: Rename

    /// Starts renaming the selected node, on the card in the network, which the lower pane
    /// switches to first when it shows the table.
    func beginRenamingSelectedNode() {
        UserDefaults.standard.set(TimelineSetting.LowerPane.network.rawValue, forKey: TimelineSetting.lowerPaneKey)
        editor.beginRename()
    }

    func rename(_ node: UInt64, to name: String) -> EditOutcome {
        guard isActive else { return .failed(String(localized: "This scene is closed.")) }
        guard let current = editor.graph.node(node) else { return .done }
        if current.name == name { return .done }
        // An undo of this step restores the old name, so the selected node has the old path then.
        let change = PathChange(from: "/" + name, to: current.path)
        return performEdit(named: String(localized: "Rename Node"), pathChange: change) {
            outcome(helix.core.renameNode(node, std.string(name)))
        }
    }

    // MARK: Copy, cut, paste, duplicate

    func copy(nodes: [UInt64]) -> EditOutcome {
        guard isActive else { return .failed(String(localized: "This scene is closed.")) }
        let result = helix.core.copyNodes(Self.idList(nodes))
        guard result.status.ok else { return failure(result.status) }
        NodePasteboard.write(String(engineBytes: result.text))
        return .done
    }

    func cut(nodes: [UInt64]) -> EditOutcome {
        // Nothing is taken off the scene unless the copy is on the pasteboard.
        let copied = copy(nodes: nodes)
        guard copied.error == nil else { return copied }
        let name = nodes.count == 1 ? String(localized: "Cut Node") : String(localized: "Cut Nodes")
        return performEdit(named: name) {
            for node in nodes {
                let status = helix.core.deleteNode(node)
                if !status.ok { return failure(status) }
            }
            return .done
        }
    }

    func paste(at canvasPoint: CGPoint?) -> EditOutcome {
        guard isActive else { return .failed(String(localized: "This scene is closed.")) }
        guard let text = NodePasteboard.read() else {
            return .failed(String(localized: "There are no nodes to paste. Copy some nodes first."))
        }
        return insert(copy: text, named: String(localized: "Paste"), at: canvasPoint)
    }

    func duplicate(nodes: [UInt64]) -> EditOutcome {
        guard isActive else { return .failed(String(localized: "This scene is closed.")) }
        // The pasteboard is not used: a duplicate leaves what the person copied where it is.
        let result = helix.core.copyNodes(Self.idList(nodes))
        guard result.status.ok else { return failure(result.status) }
        let name = nodes.count == 1 ? String(localized: "Duplicate Node") : String(localized: "Duplicate Nodes")
        return insert(copy: String(engineBytes: result.text), named: name, at: nil)
    }

    /// Makes the nodes of a copy, places them, as one step of undo.
    private func insert(copy text: String, named name: String, at target: CGPoint?) -> EditOutcome {
        var made: [UInt64] = []
        return performEdit(named: name) {
            // Made where they were copied from, then moved: the engine does not say where a copy
            // sits, and the place depends on what is already in the scene.
            let result = helix.core.pasteNodes(std.string(text), 0, 0)
            guard result.status.ok else { return failure(result.status) }
            made = Array(result.nodes)

            let new = Set(made)
            let all = Array(helix.core.listNodes())
            let position = { (node: helix.core.NodeInfo) in CGPoint(x: CGFloat(node.x), y: CGFloat(node.y)) }
            let pasted = all.filter { new.contains($0.id) }
            let delta = NodeClipboard.offset(
                for: pasted.map(position), occupied: all.filter { !new.contains($0.id) }.map(position), target: target
            )
            if delta != .zero {
                for node in pasted {
                    let status = helix.core.moveNode(node.id, node.x + Float(delta.x), node.y + Float(delta.y))
                    if !status.ok { return failure(status) }
                }
            }
            return EditOutcome(createdNodes: made)
        }
    }
}
