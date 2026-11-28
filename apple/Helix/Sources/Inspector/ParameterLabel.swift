import SwiftUI

/// The label of a row. A parameter with an explanation has an info mark after its label, and
/// the label opens the explanation. At the end of the column sit the diamond that keys the value
/// and the arrow that puts it back: they show when the pointer is over the row, and stay when
/// the value follows keyframes or is not the default.
struct ParameterLabel: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel
    let hovering: Bool
    @State private var showingHelp = false

    /// The whole column: the label and the buttons.
    static let width: CGFloat = 126
    /// What the buttons take at the end of the column.
    static let actionsWidth: CGFloat = 36

    private var hasActions: Bool { parameter.canKeyframe || parameter.canReset }

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 2) {
            text
            Spacer(minLength: 0)
            if hasActions { RowActions(parameter: parameter, model: model, hovering: hovering) }
        }
        .frame(width: Self.width, alignment: .leading)
    }

    @ViewBuilder
    private var text: some View {
        if parameter.hasHelp {
            Button { showingHelp = true } label: {
                // The mark is part of the text, so it follows the label when it wraps and takes
                // no room of its own.
                (Text(parameter.label) + Text(verbatim: "\u{00A0}") + Text(Image(systemName: "info.circle")).font(.caption2).foregroundStyle(.tertiary))
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.leading)
                    .lineLimit(2)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .buttonStyle(.plain)
            .popover(isPresented: $showingHelp, arrowEdge: .bottom) { ParameterHelpCard(parameter: parameter) }
            .accessibilityLabel(Text(parameter.label))
            .accessibilityHint(Text("Shows what this setting does"))
        } else {
            Text(parameter.label)
                .foregroundStyle(.secondary)
                .lineLimit(2)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

/// What a setting does, in plain words: its tooltip and its longer explanation.
struct ParameterHelpCard: View {
    let parameter: InspectorParameter

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(parameter.label).uiFont(.headline)
            ForEach(parameter.helpParagraphs, id: \.self) { paragraph in
                Text(paragraph)
                    .fixedSize(horizontal: false, vertical: true)
                    .textSelection(.enabled)
            }
        }
        .padding(12)
        .frame(width: 280, alignment: .leading)
        .accessibilityElement(children: .combine)
    }
}

/// The diamond and the arrow at the end of the label column.
struct RowActions: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel
    let hovering: Bool

    private var diamondShown: Bool { hovering || !parameter.animated.isEmpty }
    private var resetShown: Bool { hovering || parameter.wantsResetShown }

    var body: some View {
        HStack(spacing: 2) {
            if parameter.canKeyframe { diamond }
            if parameter.canReset { reset }
        }
        .frame(width: ParameterLabel.actionsWidth, alignment: .trailing)
    }

    // One diamond for the whole value, however many components it has: a position moves as one
    // thing, so its three numbers are keyed together, which is what the artist means by "a key
    // here". A single component is keyed from its own menu, as before.
    private var diamond: some View {
        let keyed = parameter.isKeyedHere
        return Button {
            if keyed { model.removeKeyframe(parameter.name) } else { model.setKeyframe(parameter.name) }
        } label: {
            Image(systemName: keyed ? "diamond.fill" : "diamond")
                .font(.system(size: 10, weight: .semibold))
                .foregroundStyle(parameter.animated.isEmpty ? Color.secondary : Color.orange)
                .frame(width: 17, height: 17)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .opacity(diamondShown ? 1 : 0)
        .help(keyed
            ? Text("This value has a keyframe on this frame. Click to remove it.")
            : (parameter.animated.isEmpty
                ? Text("Set a keyframe on this frame, so that the value can change over time.")
                : Text("This value follows keyframes. Click to set a keyframe on this frame.")))
        .accessibilityLabel(keyed ? Text("Remove Keyframe") : Text("Set Keyframe"))
        .accessibilityValue(parameter.animated.isEmpty ? Text("") : Text("Animated"))
    }

    private var reset: some View {
        let blocked = parameter.resetIsBlockedByKeyframes
        return Button { model.reset(parameter.name) } label: {
            Image(systemName: "arrow.counterclockwise")
                .font(.system(size: 10, weight: .semibold))
                .foregroundStyle(Color.secondary)
                .frame(width: 17, height: 17)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        // A click while the value follows keyframes says why nothing happens, as the menu does.
        .opacity(resetShown ? (blocked ? 0.45 : 1) : 0)
        .disabled(parameter.isAtDefault && parameter.kind != .ramp)
        .help(blocked
            ? Text("This value follows keyframes, which a reset would only be overwritten by. Clear its keyframes first.")
            : Text("Reset to Default"))
        .accessibilityLabel(Text("Reset to Default"))
    }
}
