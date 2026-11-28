import AppKit
import HelixCore
import MetalKit

/// The Metal view of the viewport, which turns trackpad and mouse gestures into camera moves.
///
/// The view draws on demand: every move asks for one frame, and nothing draws in between.
final class HelixMetalView: MTKView {
    weak var controller: ViewportController?
    /// Called once, when the window of the view is about to close.
    var onWindowClose: (() -> Void)?

    private var closeObserver: NSObjectProtocol?

    override var acceptsFirstResponder: Bool { true }

    // MARK: Window

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        if let closeObserver { NotificationCenter.default.removeObserver(closeObserver) }
        closeObserver = nil
        guard let window else { return }
        window.makeFirstResponder(self)
        closeObserver = NotificationCenter.default.addObserver(
            forName: NSWindow.willCloseNotification, object: window, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated { self?.onWindowClose?() }
        }
    }

    // The system theme changed between light and dark: the colours of the viewport may have.
    override func viewDidChangeEffectiveAppearance() {
        super.viewDidChangeEffectiveAppearance()
        clearColor = ViewportRenderer.palette(for: effectiveAppearance).viewportClearColor
        needsDisplay = true
    }

    // The window moved to a screen with another scale, so the drawable has another size.
    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        needsDisplay = true
    }

    // MARK: Trackpad and mouse

    // What each gesture does depends on the preset of Settings > Navigation, which is read at every
    // event so a change applies at once, in every window. The mapping itself is `NavigationMapper`;
    // this only describes the event to it and applies what it answers.

    // A pinch zooms.
    override func magnify(with event: NSEvent) {
        apply(NavigationMapper.pinch(magnification: Float(event.magnification), options: NavigationOptions.stored()))
    }

    // A two finger rotation turns the scene around the vertical axis.
    override func rotate(with event: NSEvent) {
        apply(NavigationMapper.rotation(degrees: Float(event.rotation), options: NavigationOptions.stored()))
    }

    // A scroll orbits, pans or zooms according to the preset. A Magic Mouse scrolls with precise
    // deltas like a trackpad, so the device cannot be told from the event: the preset says what the
    // person holds.
    override func scrollWheel(with event: NSEvent) {
        let scroll = NavigationScroll(
            deltaX: Float(event.scrollingDeltaX), deltaY: Float(event.scrollingDeltaY),
            precise: event.hasPreciseScrollingDeltas,
            momentum: !event.momentumPhase.isEmpty,
            modifiers: NavigationModifiers(event.modifierFlags)
        )
        apply(NavigationMapper.scroll(scroll, preset: NavigationPreset.stored(), options: NavigationOptions.stored()))
    }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
    }

    override func mouseDragged(with event: NSEvent) { drag(event, button: .left) }

    override func rightMouseDragged(with event: NSEvent) { drag(event, button: .right) }

    override func otherMouseDragged(with event: NSEvent) { drag(event, button: .middle) }

    // MARK: Keyboard

    // F frames the geometry, shift W cycles the wireframe and shift M shows or hides the materials.
    // The View menu holds the same commands with the same keys, and handles them wherever the focus
    // is: AppKit leaves a key equivalent that has no command key to the text field when one is being
    // edited, so typing an "f" in the palette search does not frame. These cases stay for the moment
    // the menu does not take the key (no document in front).
    override func keyDown(with event: NSEvent) {
        let modifiers = event.modifierFlags.intersection([.shift, .control, .option, .command])
        // Space plays, the arrows step, and option with an arrow goes to an end.
        if let timeline = controller?.timeline,
           timeline.handleKey(
               code: event.keyCode, option: modifiers == .option,
               otherModifiers: !modifiers.subtracting(.option).isEmpty
           ) {
            return
        }
        switch (event.charactersIgnoringModifiers?.lowercased(), modifiers) {
        case ("f", []):
            controller?.requestFrame()
        case ("w", [.shift]):
            controller?.cycleWireframe()
        case ("m", [.shift]):
            controller?.toggleMaterials()
        default:
            super.keyDown(with: event)
        }
    }

    // MARK: Helpers

    private func drag(_ event: NSEvent, button: NavigationButton) {
        let drag = NavigationDrag(
            deltaX: Float(event.deltaX), deltaY: Float(event.deltaY),
            button: button, modifiers: NavigationModifiers(event.modifierFlags)
        )
        apply(NavigationMapper.drag(drag, preset: NavigationPreset.stored(), options: NavigationOptions.stored()))
    }

    private func apply(_ navigation: NavigationMove?) {
        guard let navigation else { return }
        move {
            switch navigation {
            case .orbit(let x, let y): helix.core.orbitViewport(x, y)
            case .pan(let x, let y): helix.core.panViewport(x, y)
            case .dolly(let amount): helix.core.dollyViewport(amount)
            }
        }
    }

    // Moves the camera, then asks for one frame.
    private func move(_ change: () -> Void) {
        change()
        needsDisplay = true
    }
}

extension NavigationModifiers {
    init(_ flags: NSEvent.ModifierFlags) {
        self = []
        if flags.contains(.shift) { insert(.shift) }
        if flags.contains(.control) { insert(.control) }
        if flags.contains(.option) { insert(.option) }
        if flags.contains(.command) { insert(.command) }
    }
}
