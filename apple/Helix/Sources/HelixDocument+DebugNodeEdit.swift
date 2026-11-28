import CxxStdlib
import Foundation
import HelixCore

extension HelixDocument {
    /// With `HELIX_DEBUG_NODE_EDIT=1`, makes a grid and a transform the way the Add menu does, makes the
    /// transform read the grid, duplicates both, renames the first grid and undoes the rename, and
    /// says what the scene held after each step ("helix-launch: node-edit ..."). `Scripts/launch-smoke.py`
    /// reads it: the names come from the language of the launch, the copy reads the copy of the
    /// grid, a rename is followed by what reads the node, and undo puts it all back.
    func scheduleDebugNodeEditCheck() {
        guard ProcessInfo.processInfo.environment["HELIX_DEBUG_NODE_EDIT"] == "1" else { return }
        Task { @MainActor [weak self] in
            try? await Task.sleep(for: .seconds(3))
            guard let self, self.isActive,
                  let grid = self.editor.paletteTypes.first(where: { $0.name == "helix::grid" }),
                  let transform = self.editor.paletteTypes.first(where: { $0.name == "helix::transform" }) else { return }

            func names() -> [String] { helix.core.listNodes().map { String(engine: $0.name) }.sorted() }
            func expression(of node: String) -> String {
                guard let info = helix.core.listNodes().first(where: { String(engine: $0.name) == node }) else { return "" }
                let parameter = helix.core.listParameters(info.id).first { String(engine: $0.name) == "uniform_scale" }
                return parameter.flatMap { $0.expressions.first.map { String(engine: $0) } } ?? ""
            }

            // The steps of a person come one to an event of the run loop, which the undo manager
            // groups apart. Here they come in one go, so each is closed as a group of its own.
            let undo = self.undoManager
            undo?.groupsByEvent = false
            func step(_ body: () -> Void) {
                undo?.beginUndoGrouping()
                body()
                undo?.endUndoGrouping()
            }

            step { self.editor.addNode(grid) }
            step { self.editor.addNode(transform) }
            let added = names()
            guard added.count == 2, let first = helix.core.listNodes().min(by: { $0.id < $1.id }),
                  let second = helix.core.listNodes().max(by: { $0.id < $1.id }) else { return }
            let gridName = String(engine: first.name), transformName = String(engine: second.name)
            _ = helix.core.setExpression(second.id, "uniform_scale", 0, "prm(\"\(gridName).size\")")

            self.editor.selectAll()
            step { self.editor.duplicateSelection() }
            let duplicated = names()
            let copies = Set(duplicated).subtracting(added)
            let copyTransform = copies.first { $0.hasPrefix(String(transformName.dropLast(1))) } ?? ""
            let copyExpression = expression(of: copyTransform)

            self.editor.select(nodes: [first.id])
            self.editor.beginRename()
            self.editor.renameTextChanged("sol")
            step { self.editor.commitRename() }
            let renamed = expression(of: transformName)
            let renamedNames = names()
            undo?.undo()
            let undone = names()
            let selectedAfterUndo = self.editor.selectedNodes.count

            Self.debugLog("node-edit added=\(added.joined(separator: ",")) duplicated=\(duplicated.joined(separator: ",")) copy-expression=\(copyExpression) renamed=\(renamedNames.joined(separator: ",")) renamed-expression=\(renamed) undone=\(undone.joined(separator: ",")) selected-after-undo=\(selectedAfterUndo) undo=\(undo != nil)") // l10n-ok: read by launch-smoke.py
        }
    }
}
