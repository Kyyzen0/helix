import AppKit
import Combine

/// The cards of the graph as VoiceOver sees them. The canvas is one drawing, so each card is an
/// accessibility element of its own on the view that takes the events: it says its name and type,
/// whether it is shown, in error or selected, sits where the card is drawn, and offers the actions
/// of its context menu.
///
/// Everything an element says is read from the model when it is asked, so a card that moved, was
/// renamed or was selected needs no update, and a pan or a zoom moves the frames with it.
@MainActor
final class NodeAccessibilityTree {
    private unowned let view: NSView
    private weak var model: NodeEditorModel?
    private var elements: [UInt64: NodeAccessibilityElement] = [:]
    private var selectionObserver: AnyCancellable?

    init(view: NSView, model: NodeEditorModel?) {
        self.view = view
        self.model = model
        selectionObserver = model?.$selectedNodes.removeDuplicates().dropFirst().sink { [weak self] selection in
            // After the change has landed, since the publisher fires before it.
            DispatchQueue.main.async { self?.selectionChanged(selection) }
        }
    }

    /// One element per node, in the order the nodes are drawn. An element is kept for as long as
    /// its node is, so VoiceOver keeps its place.
    func children() -> [NodeAccessibilityElement] {
        guard let model else { return [] }
        var kept: [UInt64: NodeAccessibilityElement] = [:]
        let list = model.graph.nodes.map { node -> NodeAccessibilityElement in
            let element = elements[node.id] ?? NodeAccessibilityElement(node: node.id, view: view, model: model)
            kept[node.id] = element
            return element
        }
        elements = kept
        return list
    }

    /// The element of the node that has the selection, for VoiceOver to land on.
    func focusedElement() -> NodeAccessibilityElement? {
        guard let id = model?.soleSelectedNode else { return nil }
        return children().first { $0.node == id }
    }

    private func selectionChanged(_ selection: Set<UInt64>) {
        guard view.window != nil else { return }
        NSAccessibility.post(element: view, notification: .selectedChildrenChanged)
        // The key was pressed or the card clicked: say where the selection is now.
        if selection.count == 1, let element = focusedElement() {
            NSAccessibility.post(element: element, notification: .focusedUIElementChanged)
        }
    }
}

/// What a card says, read from the model when it is asked. AppKit asks on the main thread, but
/// the methods of an accessibility element carry no actor, so the answers come from this object.
@MainActor
private final class NodeElementReader {
    let node: UInt64
    private unowned let view: NSView
    private weak var model: NodeEditorModel?

    init(node: UInt64, view: NSView, model: NodeEditorModel) {
        self.node = node
        self.view = view
        self.model = model
    }

    private var card: EditorNode? { model?.graph.node(node) }

    func label() -> String? {
        guard let model, let card else { return nil }
        return NodeAccessibilityText.label(name: card.name, typeLabel: model.graph.label(of: card))
    }

    func value() -> String? {
        guard let model, let card else { return nil }
        return NodeAccessibilityText.value(
            error: card.error, warningCount: card.warnings.count,
            isDisplay: model.graph.displayNode == card.id, isSelected: model.selectedNodes.contains(card.id)
        )
    }

    func isSelected() -> Bool { model?.selectedNodes.contains(node) ?? false }

    /// Where the card is drawn, on the screen.
    func frame() -> NSRect {
        guard let model, let card else { return .zero }
        let rect = model.transform.viewRect(EditorLayout.cardRect(at: model.displayPosition(of: card)))
        guard let window = view.window else { return rect }
        return window.convertToScreen(view.convert(rect, to: nil))
    }

    func act(_ run: (NodeEditorModel, UInt64) -> Void) -> Bool {
        guard let model, card != nil else { return false }
        run(model, node)
        return true
    }

    func actions() -> [NSAccessibilityCustomAction] {
        func action(_ name: String, _ run: @escaping @MainActor (NodeEditorModel, UInt64) -> Void) -> NSAccessibilityCustomAction {
            NSAccessibilityCustomAction(name: name) { [weak self] in
                guard let self else { return false }
                return MainActor.assumeIsolated { self.act(run) }
            }
        }
        return [
            action(String(localized: "Select")) { model, id in model.select(nodes: [id]); model.reveal(id) },
            action(String(localized: "Show This Result")) { model, id in model.setDisplay(id) },
            action(String(localized: "Rename…")) { model, id in model.select(nodes: [id]); model.beginRename(id) },
            action(String(localized: "Delete")) { model, id in model.select(nodes: [id]); model.deleteSelection() },
        ]
    }
}

private struct Unchecked<T>: @unchecked Sendable {
    var value: T
}

/// A card, to VoiceOver.
final class NodeAccessibilityElement: NSAccessibilityElement {
    let node: UInt64
    private nonisolated(unsafe) let view: NSView
    private let reader: NodeElementReader

    @MainActor
    init(node: UInt64, view: NSView, model: NodeEditorModel) {
        self.node = node
        self.view = view
        self.reader = NodeElementReader(node: node, view: view, model: model)
        super.init()
    }

    override func accessibilityRole() -> NSAccessibility.Role? { .button }

    override func accessibilityRoleDescription() -> String? { String(localized: "node") }

    override func accessibilityParent() -> Any? { view }

    override func accessibilityLabel() -> String? {
        let reader = reader
        return MainActor.assumeIsolated { reader.label() }
    }

    override func accessibilityValue() -> Any? {
        let reader = reader
        return MainActor.assumeIsolated { reader.value() }
    }

    override func isAccessibilitySelected() -> Bool {
        let reader = reader
        return MainActor.assumeIsolated { reader.isSelected() }
    }

    override func accessibilityFrame() -> NSRect {
        let reader = reader
        return MainActor.assumeIsolated { reader.frame() }
    }

    /// A press selects the card, as a click does.
    override func accessibilityPerformPress() -> Bool {
        let reader = reader
        return MainActor.assumeIsolated { reader.act { model, id in model.select(nodes: [id]); model.reveal(id) } }
    }

    override func accessibilityCustomActions() -> [NSAccessibilityCustomAction]? {
        let reader = reader
        return MainActor.assumeIsolated { Unchecked(value: reader.actions()) }.value
    }
}
