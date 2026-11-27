import AppKit
import SwiftUI

/// A text field for a number: it shows the value, and what was typed is read when the person
/// presses Return or leaves the field, never on each keystroke, so a number half typed is not
/// sent to the engine.
struct NumberField: View {
    /// The value as it is shown, formatted.
    let text: String
    var isEnabled = true
    var alignment: TextAlignment = .trailing
    var accessibilityName: String = ""
    /// Called with what was typed, when it differs from the value.
    var onCommit: (String) -> Void

    @State private var draft = ""
    @FocusState private var focused: Bool

    var body: some View {
        TextField("", text: $draft)
            .textFieldStyle(.roundedBorder)
            .multilineTextAlignment(alignment)
            .monospacedDigit()
            .focused($focused)
            .disabled(!isEnabled)
            .accessibilityLabel(Text(accessibilityName))
            .onAppear { draft = text }
            .onSubmit(commit)
            .onChange(of: focused) { _, isFocused in
                if !isFocused { commit() }
            }
            .onChange(of: text) { _, shown in
                if !focused { draft = shown }
            }
    }

    private func commit() {
        guard draft != text else { return }
        onCommit(draft)
        // A refused or clamped value comes back as the value the node holds.
        draft = text
    }
}

/// A label that changes a number when it is dragged sideways, as a slider would.
struct ScrubLabel: View {
    let title: String
    var tint: Color = .secondary
    var isEnabled = true
    /// The value to start from, when a drag begins.
    let current: () -> Double
    let range: InspectorRange?
    let integer: Bool
    let begin: () -> Void
    let change: (Double) -> Void
    let end: () -> Void

    @State private var start: Double?

    var body: some View {
        Text(title)
            .font(.system(size: 11, weight: .semibold))
            .foregroundStyle(isEnabled ? tint : Color.secondary)
            .frame(width: 14)
            .contentShape(Rectangle())
            .pointerStyle(isEnabled ? .columnResize : .default)
            .gesture(
                DragGesture(minimumDistance: 2)
                    .onChanged { drag in
                        guard isEnabled else { return }
                        if start == nil {
                            start = current()
                            begin()
                        }
                        let fine = NSEvent.modifierFlags.contains(.option)
                        change(Scrub.value(from: start ?? 0, translation: drag.translation.width, range: range, integer: integer, fine: fine))
                    }
                    .onEnded { _ in
                        guard start != nil else { return }
                        start = nil
                        end()
                    }
            )
            .help(Text("Drag to change the value. Hold Option for fine control."))
    }
}

/// The expression of a component, shown in place of its value: monospaced and tinted, so it
/// cannot be mistaken for a number.
struct ExpressionField: View {
    let text: String
    var hasError = false
    var isEnabled = true
    let onEdit: () -> Void

    var body: some View {
        Button(action: onEdit) {
            Text(text)
                .font(.system(size: 11, design: .monospaced))
                .foregroundStyle(hasError ? Color.red : Color.accentColor)
                .lineLimit(1)
                .truncationMode(.middle)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal, 6)
                .padding(.vertical, 3)
                .background(
                    RoundedRectangle(cornerRadius: 5)
                        .fill((hasError ? Color.red : Color.accentColor).opacity(0.12))
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 5)
                        .stroke((hasError ? Color.red : Color.accentColor).opacity(0.5), lineWidth: 1)
                )
        }
        .buttonStyle(.plain)
        .disabled(!isEnabled)
        .help(Text("Click to edit the expression"))
    }
}

/// The field a draft expression is typed in.
struct ExpressionDraftField: View {
    @Binding var text: String
    let onCommit: () -> Void
    let onCancel: () -> Void
    @FocusState private var focused: Bool

    var body: some View {
        TextField("", text: $text, prompt: Text("Expression"))
            .font(.system(size: 11, design: .monospaced))
            .textFieldStyle(.roundedBorder)
            .focused($focused)
            .onSubmit(onCommit)
            .onExitCommand(perform: onCancel)
            .onAppear { focused = true }
            // Leaving the field commits it, like every other field of the panel.
            .onChange(of: focused) { _, isFocused in
                if !isFocused { onCommit() }
            }
    }
}

/// A small diamond for a value that follows keyframes.
struct KeyframeBadge: View {
    var body: some View {
        Image(systemName: "diamond.fill")
            .font(.system(size: 7))
            .foregroundStyle(Color.orange)
            .help(Text("This value follows keyframes. Editing keyframes comes with the timeline."))
            .accessibilityLabel(Text("Animated"))
    }
}

enum InspectorIcons {
    /// The SF Symbol that stands for an icon a node names (the names are Lucide's). An icon with
    /// no counterpart gets a neutral symbol, so a toggle is still a button.
    static func symbol(forNodeIcon name: String) -> String {
        switch name {
        case "eye": "eye"
        case "tag": "tag"
        case "layers": "square.3.layers.3d"
        case "audio-waveform": "waveform"
        case "axis-3d": "move.3d"
        case "scaling": "arrow.up.left.and.arrow.down.right"
        case "spline": "scribble.variable"
        case "disc": "circle.circle"
        case "squares-subtract": "rectangle.on.rectangle"
        default: "circle.grid.2x2"
        }
    }
}
