import AppKit
import SwiftUI

/// The search palette that makes a node: a field, and the node types that match it.
///
/// Up and down choose, Return makes the node, Escape closes. The field is an AppKit one, since
/// that is where a text field's own commands, the arrows and Return, can be told apart reliably.
struct NodePalette: View {
    @Environment(\.theme) private var theme
    @ObservedObject var model: NodeEditorModel
    let request: PaletteRequest
    let area: CGSize

    @State private var query: String
    @State private var chosen = 0

    init(model: NodeEditorModel, request: PaletteRequest, area: CGSize, query: String = "") {
        self.model = model
        self.request = request
        self.area = area
        _query = State(initialValue: query)
    }

    private static let width: CGFloat = 380
    private static let rowHeight: CGFloat = 52
    private static let maxRows = 8

    private var results: [NodeSearch.Result] {
        Array(NodeSearch.rank(query, in: model.paletteTypes).prefix(80))
    }

    var body: some View {
        let shown = results
        let visibleRows = min(max(shown.count, 1), Self.maxRows, max(Int((area.height - 80) / Self.rowHeight), 2))
        let height = 44 + CGFloat(visibleRows) * Self.rowHeight
        VStack(spacing: 0) {
            PaletteSearchField(
                text: $query,
                placeholder: String(localized: "Search nodes"),
                onMove: { step in chosen = min(max(chosen + step, 0), max(shown.count - 1, 0)) },
                onSubmit: { create(shown) },
                onCancel: { model.closePalette() }
            )
            .frame(height: 28)
            .padding(8)
            Divider()
            if shown.isEmpty {
                Text("No node matches")
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollViewReader { proxy in
                    ScrollView {
                        LazyVStack(spacing: 0) {
                            ForEach(Array(shown.enumerated()), id: \.element.type.name) { index, result in
                                row(result.type, isChosen: index == chosen)
                                    .id(index)
                                    .onTapGesture { model.addNode(result.type) }
                            }
                        }
                    }
                    .onChange(of: chosen) { _, index in proxy.scrollTo(index) }
                }
            }
        }
        .frame(width: Self.width, height: height)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 10))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color.primary.opacity(0.18)))
        .shadow(color: .black.opacity(0.25), radius: 12, y: 4)
        .onChange(of: query) { chosen = 0 }
        .offset(x: clamped(request.anchor.x, in: area.width, size: Self.width), y: clamped(request.anchor.y, in: area.height, size: height))
        .accessibilityElement(children: .contain)
        .accessibilityLabel(Text("Add node"))
    }

    private func create(_ shown: [NodeSearch.Result]) {
        guard shown.indices.contains(chosen) else { return }
        model.addNode(shown[chosen].type)
    }

    /// Keeps the panel inside the editor, at the pointer when there is room.
    private func clamped(_ origin: CGFloat, in length: CGFloat, size: CGFloat) -> CGFloat {
        max(8, min(origin, length - size - 8))
    }

    private func row(_ type: EditorNodeType, isChosen: Bool) -> some View {
        // What the node does, in a sentence, and not its internal name, which the tooltip keeps.
        VStack(alignment: .leading, spacing: 1) {
            Text(type.label).fontWeight(.medium)
            if !type.description.isEmpty {
                Text(type.description)
                    .font(.caption)
                    .foregroundStyle(isChosen ? .primary : .secondary)
                    .lineLimit(2)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 10)
        .frame(height: Self.rowHeight)
        .background(isChosen ? theme.accent.opacity(0.3) : Color.clear)
        .contentShape(Rectangle())
        .help(type.name)
        .accessibilityElement(children: .combine)
    }
}

/// A text field that tells the palette about the arrows, Return and Escape, and takes the keys
/// as soon as it is shown.
struct PaletteSearchField: NSViewRepresentable {
    @Binding var text: String
    let placeholder: String
    let onMove: (Int) -> Void
    let onSubmit: () -> Void
    let onCancel: () -> Void

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    func makeNSView(context: Context) -> NSTextField {
        let field = NSTextField()
        field.placeholderString = placeholder
        field.isBordered = false
        field.drawsBackground = false
        field.focusRingType = .none
        field.font = .systemFont(ofSize: 15)
        field.delegate = context.coordinator
        // The view is in a window only after this returns.
        DispatchQueue.main.async { field.window?.makeFirstResponder(field) }
        return field
    }

    func updateNSView(_ field: NSTextField, context: Context) {
        context.coordinator.parent = self
        if field.stringValue != text { field.stringValue = text }
    }

    @MainActor
    final class Coordinator: NSObject, NSTextFieldDelegate {
        var parent: PaletteSearchField
        init(_ parent: PaletteSearchField) { self.parent = parent }

        func controlTextDidChange(_ notification: Notification) {
            guard let field = notification.object as? NSTextField else { return }
            parent.text = field.stringValue
        }

        func control(_ control: NSControl, textView: NSTextView, doCommandBy selector: Selector) -> Bool {
            switch selector {
            case #selector(NSResponder.moveUp(_:)): parent.onMove(-1)
            case #selector(NSResponder.moveDown(_:)): parent.onMove(1)
            case #selector(NSResponder.insertNewline(_:)): parent.onSubmit()
            case #selector(NSResponder.cancelOperation(_:)): parent.onCancel()
            default: return false
            }
            return true
        }
    }
}
