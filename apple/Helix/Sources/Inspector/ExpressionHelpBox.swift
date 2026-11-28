import SwiftUI

/// The help under a field while an expression is typed in it: the variables and functions, each
/// with an example that a click puts in the field.
struct ExpressionHelpBox: View {
    @ObservedObject var model: InspectorModel
    @Environment(\.theme) private var theme

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Write a formula. It is read again on every frame.")
                .uiFont(.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            ForEach(ExpressionHelp.items) { item in
                VStack(alignment: .leading, spacing: 1) {
                    HStack(alignment: .firstTextBaseline, spacing: 6) {
                        Text(item.syntax)
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundStyle(theme.accent)
                        Text(item.summary)
                            .uiFont(.caption)
                            .foregroundStyle(.secondary)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    // A tap, not a button: the field keeps the keyboard, so the text typed
                    // is not sent to the node because the click took it away.
                    Text(item.example)
                        .font(.system(size: 11, design: .monospaced))
                        .padding(.horizontal, 5).padding(.vertical, 1)
                        .background(RoundedRectangle(cornerRadius: 4).fill(Color.secondary.opacity(0.15)))
                        .onTapGesture { model.expressionDraft?.text = item.example }
                        .help(Text("Use this example"))
                        .accessibilityAddTraits(.isButton)
                        .accessibilityLabel(Text("Use the example \(item.example)"))
                }
            }
        }
        .padding(8)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 7).fill(Color.secondary.opacity(0.08)))
        .overlay(RoundedRectangle(cornerRadius: 7).stroke(Color.secondary.opacity(0.3)))
    }
}
