import AppKit
import SwiftUI

/// The window of a document: the viewport, with a toolbar over it.
struct DocumentView: View {
    @ObservedObject var document: HelixDocument
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        ZStack {
            // The viewport is made once the document holds the engine, since it draws what the
            // engine holds.
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
        .frame(minWidth: 480, minHeight: 320)
        .task { activate() }
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
