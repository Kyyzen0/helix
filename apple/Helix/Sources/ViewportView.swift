import MetalKit
import SwiftUI

/// The viewport of a document: a Metal view that draws only when asked.
struct ViewportView: NSViewRepresentable {
    let document: HelixDocument

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeNSView(context: Context) -> HelixMetalView {
        let view = HelixMetalView()
        view.controller = document.viewport
        document.viewport.view = view

        // Draws when a gesture or the document asks, not on a timer.
        view.enableSetNeedsDisplay = true
        view.isPaused = true
        view.colorPixelFormat = .bgra8Unorm
        view.colorspace = CGColorSpace(name: CGColorSpace.sRGB)
        view.clearColor = MTLClearColor(
            red: Double(ViewportRenderer.background.red),
            green: Double(ViewportRenderer.background.green),
            blue: Double(ViewportRenderer.background.blue),
            alpha: 1
        )
        view.onWindowClose = { [document] in document.relinquish() }
        document.viewport.onFailure = { [document] message in document.reportViewportFailure(message) }

        if let device = document.startViewport() {
            view.device = device
            if let renderer = ViewportRenderer(device: device, view: view, controller: document.viewport) {
                context.coordinator.renderer = renderer
                view.delegate = renderer
            } else {
                document.reportViewportFailure(String(localized: "The viewport could not load its shaders."))
            }
        }
        document.viewport.requestFrame()
        return view
    }

    func updateNSView(_ view: HelixMetalView, context: Context) {}

    /// Keeps the renderer alive, since the view only holds its delegate weakly.
    final class Coordinator {
        var renderer: ViewportRenderer?
    }
}
