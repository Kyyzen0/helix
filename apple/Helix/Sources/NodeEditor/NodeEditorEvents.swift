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

final class NodeEditorEventView: NSView {
    weak var model: NodeEditorModel? {
        didSet { observePalette() }
    }

    private var paletteObserver: AnyCancellable?

    // The view is flipped so a point of the view is a point of the canvas drawing: y grows down.
    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    // MARK: Tracking

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        for area in trackingAreas { removeTrackingArea(area) }
        addTrackingArea(NSTrackingArea(
            rect: .zero,
            options: [.mouseMoved, .activeInKeyWindow, .inVisibleRect],
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
        switch event.keyCode {
        case 51, 117: // delete, forward delete
            model?.deleteSelection()
        case 48 where flags.isEmpty: // tab
            model?.openPalette()
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

    // MARK: Context menu

    override func menu(for event: NSEvent) -> NSMenu? {
        guard let model else { return nil }
        window?.makeFirstResponder(self)
        let menu = NSMenu()
        switch model.context(at: point(of: event)) {
        case .node(let id):
            menu.addItem(item(String(localized: "Set Display Node")) { model.setDisplay(id) })
            menu.addItem(.separator())
            menu.addItem(item(String(localized: "Delete")) { model.deleteSelection() })
        case .link:
            menu.addItem(item(String(localized: "Delete Link")) { model.deleteSelection() })
        case .canvas:
            let anchor = point(of: event)
            menu.addItem(item(String(localized: "Add Node…")) { model.openPalette(at: anchor) })
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
