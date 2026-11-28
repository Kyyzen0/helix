import AppKit
import Combine
import SwiftUI

/// The view that takes the mouse, trackpad and keyboard for the node editor. It sits over the
/// canvas and draws nothing: every gesture goes to the model, which decides what it means.
///
/// AppKit takes the events because SwiftUI gestures do not give a trackpad scroll, a pinch at
/// the pointer, the right button, a click count and the keys of the view in one place.
struct NodeEditorEvents: NSViewRepresentable {
    let model: NodeEditorModel

    func makeNSView(context: Context) -> NodeEditorEventView {
        let view = NodeEditorEventView()
        view.model = model
        return view
    }

    func updateNSView(_ view: NodeEditorEventView, context: Context) {
        view.model = model
    }
}

final class NodeEditorEventView: NSView, NSViewToolTipOwner {
    weak var model: NodeEditorModel? {
        didSet {
            observePalette()
            observeDiagnostics()
            if oldValue !== model { accessibilityTree = NodeAccessibilityTree(view: self, model: model) }
        }
    }

    /// The cards, as the accessibility elements VoiceOver reads.
    private var accessibilityTree: NodeAccessibilityTree?

    private var paletteObserver: AnyCancellable?
    private var diagnosticsObserver: AnyCancellable?
    private var toolTipsPending = false

    // The view is flipped so a point of the view is a point of the canvas drawing: y grows down.
    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    // MARK: Accessibility

    // The canvas is one drawing, so the cards are elements of this view, which sits over it.
    override func isAccessibilityElement() -> Bool { true }
    override func accessibilityRole() -> NSAccessibility.Role? { .group }
    override func accessibilityLabel() -> String? { String(localized: "Node editor") }
    override func accessibilityChildren() -> [Any]? { accessibilityTree?.children() }
    override func accessibilitySelectedChildren() -> [Any]? {
        accessibilityTree?.children().filter { $0.isAccessibilitySelected() }
    }

    // MARK: Tracking

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        for area in trackingAreas { removeTrackingArea(area) }
        addTrackingArea(NSTrackingArea(
            rect: .zero,
            options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
            owner: self
        ))
    }

    /// The keys go back to the editor when the palette closes, since its text field had them.
    private func observePalette() {
        paletteObserver = model?.$palette.removeDuplicates().sink { [weak self] request in
            guard request == nil else { return }
            DispatchQueue.main.async {
                guard let self, let window = self.window else { return }
                // Only when the palette's field still has the keys, so a click elsewhere keeps
                // the focus where the person put it.
                if window.firstResponder is NSTextView || window.firstResponder is NSTextField {
                    window.makeFirstResponder(self)
                }
            }
        }
    }

    // MARK: Tooltips

    /// The message of a failed or warned node shows when the pointer rests on its card. The
    /// cards move with the view and with the graph, so the areas are made again after a change.
    private func observeDiagnostics() {
        diagnosticsObserver = model?.objectWillChange.sink { [weak self] _ in
            guard let self, !self.toolTipsPending else { return }
            self.toolTipsPending = true
            // After the change has landed, since the publisher fires before it.
            DispatchQueue.main.async {
                self.toolTipsPending = false
                self.updateToolTips()
            }
        }
    }

    override func layout() {
        super.layout()
        updateToolTips()
    }

    private func updateToolTips() {
        removeAllToolTips()
        guard let model else { return }
        for rect in model.eyeAreas() { addToolTip(rect, owner: self, userData: nil) }
        for area in model.diagnosticAreas() {
            addToolTip(area.rect, owner: self, userData: nil)
        }
    }

    // NSViewToolTipOwner
    func view(_ view: NSView, stringForToolTip tag: NSView.ToolTipTag, point: NSPoint, userData data: UnsafeMutableRawPointer?) -> String {
        guard let model else { return "" }
        // The eye comes first: it sits on the card, which may carry a message of its own.
        if model.eyeAreas().contains(where: { $0.contains(point) }) { return String(localized: "Show this result in the 3D view") }
        return model.diagnosticAreas().first { $0.rect.contains(point) }?.text ?? ""
    }

    private func point(of event: NSEvent) -> CGPoint {
        convert(event.locationInWindow, from: nil)
    }

    private func modifiers(of event: NSEvent) -> EditorModifiers {
        var result: EditorModifiers = []
        if event.modifierFlags.contains(.shift) { result.insert(.shift) }
        if event.modifierFlags.contains(.command) { result.insert(.command) }
        return result
    }

    // MARK: Mouse

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        if event.modifierFlags.contains(.control) {
            // A control click is a right click on a Mac.
            if let menu = menu(for: event) { NSMenu.popUpContextMenu(menu, with: event, for: self) }
            return
        }
        model?.mouseDown(at: point(of: event), clickCount: event.clickCount, modifiers: modifiers(of: event))
    }

    override func mouseDragged(with event: NSEvent) {
        model?.mouseDragged(to: point(of: event))
    }

    override func mouseUp(with event: NSEvent) {
        model?.mouseUp(at: point(of: event))
    }

    override func mouseMoved(with event: NSEvent) {
        model?.mouseMoved(to: point(of: event))
    }

    // A paste goes under the pointer only while it is over the editor.
    override func mouseEntered(with event: NSEvent) {
        model?.pointerInside = true
    }

    override func mouseExited(with event: NSEvent) {
        model?.mouseExited()
    }

    // The middle button pans.
    override func otherMouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        model?.beginPan(at: point(of: event))
    }

    override func otherMouseDragged(with event: NSEvent) {
        model?.mouseDragged(to: point(of: event))
    }

    override func otherMouseUp(with event: NSEvent) {
        model?.mouseUp(at: point(of: event))
    }

    // MARK: Trackpad

    // Two fingers pan. Command and a mouse wheel zoom, around the pointer.
    override func scrollWheel(with event: NSEvent) {
        guard let model else { return }
        let location = point(of: event)
        if event.hasPreciseScrollingDeltas, !event.modifierFlags.contains(.command) {
            model.pan(by: CGPoint(x: event.scrollingDeltaX, y: event.scrollingDeltaY))
        } else {
            let scale: CGFloat = event.hasPreciseScrollingDeltas ? 0.01 : 0.1
            model.zoom(by: exp(event.scrollingDeltaY * scale), around: location)
        }
    }

    // A pinch zooms around the pointer.
    override func magnify(with event: NSEvent) {
        model?.zoom(by: 1 + event.magnification, around: point(of: event))
    }

    // MARK: Keyboard

    // Single keys are handled here, and not by the menu, since a menu shortcut without a command
    // key would also fire while a text field has the keys.
    override func keyDown(with event: NSEvent) {
        let flags = event.modifierFlags.intersection([.shift, .control, .option, .command])
        // The arrows move the selection from card to card, and shift adds the card to it. Option
        // with an arrow still goes to the ends of the timeline, and the space bar still plays: the
        // frame by frame steps of the plain arrows are the Timeline menu's and the ruler's.
        if let direction = NodeNavigation.Direction(keyCode: event.keyCode),
           flags.subtracting(.shift).isEmpty, model?.moveSelection(direction, extending: flags.contains(.shift)) == true {
            return
        }
        if let timeline = model?.timeline,
           timeline.handleKey(code: event.keyCode, option: flags == .option, otherModifiers: !flags.subtracting(.option).isEmpty) {
            return
        }
        switch event.keyCode {
        case 51, 117: // delete, forward delete
            model?.deleteSelection()
        case 48 where flags.isEmpty: // tab
            model?.openPalette()
        case 36 where flags.isEmpty, 76 where flags.isEmpty: // return, enter
            if model?.soleSelectedNode != nil { model?.beginRename() } else { super.keyDown(with: event) }
        default:
            if flags.isEmpty, event.charactersIgnoringModifiers?.lowercased() == "h" {
                model?.frameSelection()
            } else {
                super.keyDown(with: event)
            }
        }
    }

    // Command A reaches here before the menu, when the editor has the keys.
    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        guard window?.firstResponder === self,
              event.modifierFlags.intersection([.shift, .control, .option, .command]) == .command,
              event.charactersIgnoringModifiers == "a"
        else { return super.performKeyEquivalent(with: event) }
        model?.selectAll()
        return true
    }

    // The Edit menu reaches the editor through the responder chain.
    @objc override func selectAll(_ sender: Any?) {
        model?.selectAll()
    }

    @objc func delete(_ sender: Any?) {
        model?.deleteSelection()
    }

    // Copy, Cut and Paste of the Edit menu reach the nodes here, and the text fields keep their
    // own while they have the keys: the responder chain gives them the command first.
    @objc func copy(_ sender: Any?) {
        model?.copySelection()
    }

    @objc func cut(_ sender: Any?) {
        model?.cutSelection()
    }

    @objc func paste(_ sender: Any?) {
        model?.pasteFromPasteboard()
    }

    @objc func duplicate(_ sender: Any?) {
        model?.duplicateSelection()
    }

    // MARK: Context menu

    override func menu(for event: NSEvent) -> NSMenu? {
        guard let model else { return nil }
        window?.makeFirstResponder(self)
        let menu = NSMenu()
        // An item that cannot apply (Paste with nothing to paste) is greyed by the code above, not
        // by asking the target, which would find the action and enable it.
        menu.autoenablesItems = false
        switch model.context(at: point(of: event)) {
        case .node(let id):
            menu.addItem(item(String(localized: "Rename…")) { model.beginRename(id) })
            menu.addItem(item(String(localized: "Duplicate")) { model.duplicateSelection() })
            menu.addItem(.separator())
            menu.addItem(item(String(localized: "Copy")) { model.copySelection() })
            menu.addItem(item(String(localized: "Cut")) { model.cutSelection() })
            menu.addItem(.separator())
            menu.addItem(item(String(localized: "Show This Result")) { model.setDisplay(id) })
            menu.addItem(.separator())
            menu.addItem(item(String(localized: "Delete")) { model.deleteSelection() })
        case .link:
            menu.addItem(item(String(localized: "Delete Link")) { model.deleteSelection() })
        case .canvas:
            let anchor = point(of: event)
            let paste = item(String(localized: "Paste")) { model.pasteFromPasteboard() }
            paste.isEnabled = model.canPaste
            menu.addItem(paste)
            menu.addItem(item(String(localized: "Add Node…")) { model.openPalette(at: anchor) })
            let tidy = item(String(localized: "Tidy Graph")) { model.tidyGraph() }
            tidy.isEnabled = model.canTidy
            menu.addItem(tidy)
            menu.addItem(.separator())
            for group in NodeCategory.groups(of: model.paletteTypes) {
                let submenu = NSMenu()
                for type in group.types {
                    let entry = item(type.label) { model.addNode(type, at: anchor) }
                    entry.image = NSImage(systemSymbolName: NodeCategory.symbol(of: type), accessibilityDescription: nil)
                    entry.toolTip = type.description.isEmpty ? nil : type.description
                    submenu.addItem(entry)
                }
                let parent = NSMenuItem(title: group.category.title, action: nil, keyEquivalent: "")
                parent.image = NSImage(systemSymbolName: group.category.symbol, accessibilityDescription: nil)
                parent.submenu = submenu
                menu.addItem(parent)
            }
        }
        return menu
    }

    private func item(_ title: String, _ action: @escaping () -> Void) -> NSMenuItem {
        let item = NSMenuItem(title: title, action: #selector(runMenuAction(_:)), keyEquivalent: "")
        item.target = self
        item.representedObject = action
        return item
    }

    @objc private func runMenuAction(_ sender: NSMenuItem) {
        (sender.representedObject as? () -> Void)?()
    }
}

// What the Edit menu may do while the editor has the keys: copy and cut need nodes selected, and
// paste needs nodes on the pasteboard.
extension NodeEditorEventView: NSUserInterfaceValidations {
    func validateUserInterfaceItem(_ item: any NSValidatedUserInterfaceItem) -> Bool {
        guard let model else { return false }
        switch item.action {
        case #selector(copy(_:)), #selector(cut(_:)), #selector(delete(_:)), #selector(duplicate(_:)):
            return !model.selectedNodes.isEmpty
        case #selector(paste(_:)):
            return model.canPaste
        case #selector(selectAll(_:)):
            return !model.graph.nodes.isEmpty
        default:
            return true
        }
    }
}
