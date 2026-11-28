import SwiftUI

/// The items of the Node menu of the menu bar: adding a node, and what is done to the selected
/// ones. The items follow the selection of the editor of the window in front, so each is greyed
/// when it does not apply: nothing selected, or no window.
struct NodeMenuContent: View {
    let document: HelixDocument?

    var body: some View {
        if let document {
            NodeMenuItems(document: document, editor: document.editor)
        } else {
            Button("Add Node…") {}.disabled(true)
            Divider()
            Button("Rename…") {}.disabled(true)
            Button("Duplicate") {}.disabled(true)
            Button("Show This Result") {}.disabled(true)
            Button("Delete") {}.disabled(true)
            Divider()
            Button("Tidy Graph") {}.disabled(true)
            Button("Frame Nodes") {}.disabled(true)
        }
    }
}

private struct NodeMenuItems: View {
    @ObservedObject var document: HelixDocument
    @ObservedObject var editor: NodeEditorModel

    private var isActive: Bool { document.isActive }
    private var hasSelection: Bool { !editor.selectedNodes.isEmpty }
    /// The one node that Rename and Show This Result are about.
    private var soleNode: UInt64? { editor.soleSelectedNode }

    var body: some View {
        Button("Add Node…") { document.showNodePalette() }
            .keyboardShortcut("k", modifiers: .command)
            .disabled(!isActive)
        Divider()
        if isActive {
            AddNodeSubmenus(types: editor.paletteTypes) { editor.addNode($0) }
        }
        Divider()
        Button("Rename…") { document.beginRenamingSelectedNode() }
            .disabled(!isActive || soleNode == nil)
        Button("Duplicate") { editor.duplicateSelection() }
            .keyboardShortcut("d", modifiers: .command)
            .disabled(!isActive || !hasSelection)
        Button("Show This Result") { if let soleNode { editor.setDisplay(soleNode) } }
            .disabled(!isActive || soleNode == nil)
        Button("Delete") { editor.deleteSelection() }
            .disabled(!isActive || (!hasSelection && editor.selectedLinks.isEmpty))
        Divider()
        // Shift L, shown by the menu. AppKit keeps a key without a command key from the menu while
        // a text field is being edited, so a capital L typed in a field is safe.
        Button("Tidy Graph") { editor.tidyGraph() }
            .keyboardShortcut("l", modifiers: .shift)
            .disabled(!isActive || !editor.canTidy)
        // The key of the editor, which the menu shows. AppKit keeps a key without a command key
        // from the menu while a text field is being edited, so typing an "h" in a field is safe.
        Button("Frame Nodes") { editor.frameSelection() }
            .keyboardShortcut("h", modifiers: [])
            .disabled(!isActive)
    }
}
