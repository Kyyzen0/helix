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

    // MARK: Trackpad

    // A pinch zooms.
    override func magnify(with event: NSEvent) {
        move { helix.core.dollyViewport(Float(event.magnification) * NavigationSettings.pinchZoom) }
    }

    // A two finger rotation turns the scene around the vertical axis.
    override func rotate(with event: NSEvent) {
        move { helix.core.orbitViewport(Float(event.rotation) * NavigationSettings.rotationPerDegree, 0) }
    }

    // A two finger scroll orbits, with shift it pans and with command it zooms. A wheel without
    // precise deltas, which is a mouse wheel, zooms.
    override func scrollWheel(with event: NSEvent) {
        let deltaX = Float(event.scrollingDeltaX)
        let deltaY = Float(event.scrollingDeltaY)

        guard event.hasPreciseScrollingDeltas else {
            move { helix.core.dollyViewport(deltaY * NavigationSettings.wheelZoomPerLine) }
            return
        }

        let modifiers = event.modifierFlags
        if modifiers.contains(.command) {
            move { helix.core.dollyViewport(deltaY * NavigationSettings.scrollZoomPerPoint) }
        } else if modifiers.contains(.shift) {
            move { helix.core.panViewport(deltaX * NavigationSettings.panPerPoint, deltaY * NavigationSettings.panPerPoint) }
        } else {
            move { helix.core.orbitViewport(deltaX * NavigationSettings.orbitPerPoint, deltaY * NavigationSettings.orbitPerPoint) }
        }
    }

    // MARK: Mouse

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
    }

    override func mouseDragged(with event: NSEvent) {
        let modifiers = event.modifierFlags
        if modifiers.contains(.control) {
            dragZoom(event)
        } else if modifiers.contains(.option) {
            dragPan(event)
        } else {
            move {
                helix.core.orbitViewport(
                    Float(event.deltaX) * NavigationSettings.orbitPerPoint,
                    Float(event.deltaY) * NavigationSettings.orbitPerPoint
                )
            }
        }
    }

    override func rightMouseDragged(with event: NSEvent) {
        dragZoom(event)
    }

    override func otherMouseDragged(with event: NSEvent) {
        dragPan(event)
    }

    // MARK: Keyboard

    // F frames the geometry, like the 3D tools it sits next to, shift W cycles the wireframe and
    // shift M shows or hides the materials. They are handled here and not as menu shortcuts: a
    // menu item with a key and no command key would also fire while a text field, such as the
    // search of the node palette, has the keys. The menu holds the same commands, without keys.
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

    private func dragZoom(_ event: NSEvent) {
        move { helix.core.dollyViewport(Float(event.deltaY) * NavigationSettings.dragZoomPerPoint) }
    }

    private func dragPan(_ event: NSEvent) {
        move {
            helix.core.panViewport(
                Float(event.deltaX) * NavigationSettings.panPerPoint,
                Float(event.deltaY) * NavigationSettings.panPerPoint
            )
        }
    }

    // Moves the camera, then asks for one frame.
    private func move(_ change: () -> Void) {
        change()
        needsDisplay = true
    }
}
