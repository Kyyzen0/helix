import SwiftUI

/// The node editor of a document: the canvas, the events over it, the palette and the message
/// an edit the engine refused leaves.
struct NodeEditorView: View {
    @ObservedObject var model: NodeEditorModel

    var body: some View {
        ZStack(alignment: .topLeading) {
            NodeEditorCanvas(model: model)
            NodeEditorEvents(model: model)
            if let request = model.palette {
                NodePalette(model: model, request: request, area: model.viewSize)
            }
            if let message = model.message {
                Text(message)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 8)
                    .background(.regularMaterial, in: Capsule())
                    .overlay(Capsule().stroke(Color.primary.opacity(0.15)))
                    .padding(12)
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
                    .allowsHitTesting(false)
                    .accessibilityAddTraits(.updatesFrequently)
            }
        }
        .onGeometryChange(for: CGSize.self) { $0.size } action: { model.setViewSize($0) }
        .clipped()
    }
}
