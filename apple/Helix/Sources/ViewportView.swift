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
        view.clearColor = ViewportRenderer.palette().viewportClearColor
        context.coordinator.watchTheme(of: view)
        // Weak, because the document owns the viewport that owns the failure handler: a strong
        // capture would keep a closed document alive.
        view.onWindowClose = { [weak document] in document?.relinquish() }
        document.viewport.onFailure = { [weak document] message in document?.reportViewportFailure(message) }

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
    @MainActor
    final class Coordinator {
        var renderer: ViewportRenderer?
        nonisolated(unsafe) private var observer: NSObjectProtocol?

        /// Redraws the view with the colours of a new theme as soon as one is chosen in the
        /// Settings window. The renderer reads the theme at each frame; the clear colour of the
        /// view, which shows before the first frame, is set here. A theme that follows the
        /// system appearance is redrawn by the view itself (`HelixMetalView`).
        func watchTheme(of view: HelixMetalView) {
            observer = NotificationCenter.default.addObserver(
                forName: UserDefaults.didChangeNotification, object: nil, queue: .main
            ) { [weak view] _ in
                MainActor.assumeIsolated {
                    guard let view else { return }
                    view.clearColor = ViewportRenderer.palette(for: view.effectiveAppearance).viewportClearColor
                    view.needsDisplay = true
                }
            }
        }

        deinit {
            if let observer { NotificationCenter.default.removeObserver(observer) }
        }
    }
}
