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
            if let session = model.renaming {
                NodeRenameField(model: model, session: session)
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

/// The name of a node being typed over its card. Return takes it, Escape and a click elsewhere
/// leave the old one, and a name that cannot be taken stays here with the reason under it.
struct NodeRenameField: View {
    @ObservedObject var model: NodeEditorModel
    let session: RenameSession
    @Environment(\.interfaceSize) private var size
    @FocusState private var focused: Bool

    var body: some View {
        if let node = model.graph.node(session.node) {
            let card = model.transform.viewRect(EditorLayout.cardRect(at: model.displayPosition(of: node)))
            let width = max(card.width, size.points(190))
            VStack(alignment: .leading, spacing: 4) {
                TextField("Name", text: Binding(get: { session.text }, set: { model.renameTextChanged($0) }))
                    .textFieldStyle(.roundedBorder)
                    .focused($focused)
                    .onSubmit { model.commitRename() }
                    .onExitCommand { model.cancelRename() }
                    .onAppear { focused = true }
                    .onChange(of: focused) { _, isFocused in
                        if !isFocused { model.cancelRename() }
                    }
                    .accessibilityLabel(Text("Node name"))
                if let error = session.error {
                    Text(error)
                        .uiFont(.caption)
                        .foregroundStyle(.red)
                        .fixedSize(horizontal: false, vertical: true)
                        .padding(.horizontal, 8)
                        .padding(.vertical, 4)
                        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 6))
                }
            }
            .frame(width: width, alignment: .leading)
            .position(x: card.midX, y: card.midY + (session.error == nil ? 0 : size.points(18)))
        }
    }
}
