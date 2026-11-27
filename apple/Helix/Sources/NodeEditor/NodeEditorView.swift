import SwiftUI

/// The node editor of a document: the canvas, the events over it, the palette and the message
/// an edit the engine refused leaves.
struct NodeEditorView: View {
    @ObservedObject var model: NodeEditorModel
    @Environment(\.interfaceSize) private var size

    var body: some View {
        ZStack(alignment: .topLeading) {
            NodeEditorCanvas(model: model)
            NodeEditorEvents(model: model)
            if model.graph.nodes.isEmpty, model.palette == nil {
                NetworkEmptyHint()
            }
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
        .onChange(of: size, initial: true) { model.setInterfaceScale(size.scale) }
        .clipped()
    }
}

/// What an empty network says: how to make a node.
struct NetworkEmptyHint: View {
    @Environment(\.interfaceSize) private var size

    var body: some View {
        VStack(spacing: size.points(8)) {
            Image(systemName: "point.3.connected.trianglepath.dotted")
                .font(.system(size: size.points(28)))
                .foregroundStyle(.tertiary)
            Text("Add a node with the buttons above, Tab, or a right click — or drop a 3D file here.")
                .uiFont(.body)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
        }
        .frame(maxWidth: size.points(360))
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .allowsHitTesting(false)
        .accessibilityElement(children: .combine)
    }
}
