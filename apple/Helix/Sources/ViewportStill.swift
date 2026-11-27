import AppKit
import SwiftUI

/// A picture that stands for the Metal viewport when a window is drawn off screen
/// (`--render-window`): the layer of a Metal view is not read back by a window snapshot.
@MainActor
final class ViewportStill: ObservableObject {
    @Published var image: NSImage?
    /// How large the viewport is in the window, in points, as the layout gave it.
    var size = CGSize.zero
}

private struct ViewportStillKey: EnvironmentKey {
    static let defaultValue: ViewportStill? = nil
}

extension EnvironmentValues {
    var viewportStill: ViewportStill? {
        get { self[ViewportStillKey.self] }
        set { self[ViewportStillKey.self] = newValue }
    }
}

struct StillViewport: View {
    @ObservedObject var still: ViewportStill

    var body: some View {
        ZStack {
            Color(red: 0.13, green: 0.14, blue: 0.16)
            if let image = still.image {
                Image(nsImage: image).resizable().scaledToFill()
            }
        }
        .clipped()
        .onGeometryChange(for: CGSize.self) { $0.size } action: { still.size = $0 }
    }
}
