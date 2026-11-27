import MetalKit

/// What the document and the menu use to reach the viewport of a window.
///
/// The camera itself lives in the facade. The controller only holds the view, so that a request
/// for a new frame has somewhere to go, and the one request that has to wait for the size of the
/// view to be known.
@MainActor
final class ViewportController {
    weak var view: MTKView?

    nonisolated init() {}

    /// Set when the camera should frame the geometry on the next frame drawn, since the aspect
    /// ratio it needs is only known there.
    private(set) var framePending = false

    /// Called when the renderer cannot draw, with a message for a person.
    var onFailure: ((String) -> Void)?

    func requestFrame() {
        framePending = true
        redraw()
    }

    func takePendingFrame() -> Bool {
        defer { framePending = false }
        return framePending
    }

    func redraw() {
        view?.needsDisplay = true
    }
}
