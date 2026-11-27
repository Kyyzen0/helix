import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// The window of a document: the viewport over the node editor, with a toolbar.
struct DocumentView: View {
    @ObservedObject var document: HelixDocument
    @Environment(\.dismiss) private var dismiss
    @Environment(\.undoManager) private var undoManager
    /// Whether the inspector column is shown. Kept across launches, and shared with the menu.
    @AppStorage(InspectorSetting.defaultsKey) private var inspectorShown = true

    var body: some View {
        VSplitView {
            ZStack {
                // The viewport is made once the document holds the engine, since it draws what
                // the engine holds.
                if document.isActive {
                    ViewportView(document: document)
                }
                if let message = document.viewportMessage {
                    Text(message)
                        .font(.title3)
                        .foregroundStyle(.secondary)
                        .padding()
                        .allowsHitTesting(false)
                }
            }
            .frame(minHeight: 160)
            NodeEditorView(model: document.editor)
                .frame(minHeight: 120, idealHeight: 260)
        }
        .frame(minWidth: 480, minHeight: 440)
        .inspector(isPresented: $inspectorShown) {
            InspectorView(model: document.inspector)
                .inspectorColumnWidth(min: 280, ideal: 320, max: 460)
        }
        // The window's undo manager takes the steps of the node editor, and marks the document
        // as edited when it gets one.
        .onAppear { document.undoManager = undoManager }
        .onChange(of: undoManager) { _, manager in document.undoManager = manager }
        .task { activate() }
        // A 3D file dropped on the window is imported into the scene.
        .onDrop(of: [.fileURL], isTargeted: nil) { providers in
            guard document.isActive else { return false }
            guard let provider = providers.first(where: { $0.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) }) else {
                return false
            }
            _ = provider.loadObject(ofClass: URL.self) { url, _ in
                guard let url else { return }
                Task { @MainActor in document.importDroppedFile(url) }
            }
            return true
        }
        .toolbar {
            ToolbarItem(placement: .navigation) {
                Label(document.displayNodeName ?? String(localized: "No display node"), systemImage: "cube")
                    .labelStyle(.titleAndIcon)
                    .help("The node the viewport shows")
            }
            ToolbarItem(placement: .principal) {
                HStack {
                    Text("\(Int(document.pointCount)) points")
                    Text("\(Int(document.triangleCount)) triangles")
                }
                .monospacedDigit()
                .foregroundStyle(.secondary)
            }
            ToolbarItem(placement: .primaryAction) {
                Button {
                    document.editor.openPalette()
                } label: {
                    Label("Add Node", systemImage: "plus")
                }
                .disabled(!document.isActive)
                .help("Add a node (Tab in the node editor, ⌘K)")
            }
            ToolbarItem(placement: .primaryAction) {
                Button {
                    inspectorShown.toggle()
                } label: {
                    Label("Inspector", systemImage: "sidebar.trailing")
                }
                .help("Show or hide the inspector (⌥⌘I)")
            }
            ToolbarItem(placement: .primaryAction) {
                Button {
                    document.requestFrame()
                } label: {
                    Label("Frame", systemImage: "viewfinder")
                }
                .help("Frame the geometry (F)")
            }
        }
        .alert(
            "Helix could not do that",
            isPresented: Binding(
                get: { document.presentedError != nil },
                set: { if !$0 { document.presentedError = nil } }
            ),
            presenting: document.presentedError
        ) { _ in
            Button("OK", role: .cancel) {}
        } message: { message in
            Text(message)
        }
    }
}

extension DocumentView {
    /// A new scene takes the engine when it is shown. When another scene holds it, there is
    /// nowhere to put this one: say so, and close the empty window.
    private func activate() {
        do {
            try document.activate()
        } catch HelixError.busy {
            NSApplication.shared.presentError(HelixError.busy)
            dismiss()
        } catch {
            document.presentedError = error.localizedDescription
        }
    }
}
