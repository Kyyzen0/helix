import SwiftUI

/// A yes or no: a checkbox, a switch or an icon button, by the style of the parameter.
struct ToggleControl: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel
    let availability: RowAvailability

    private var isOn: Bool { (parameter.ints.first ?? 0) != 0 }

    var body: some View {
        ComponentSlot(parameter: parameter, model: model, availability: availability, component: 0) {
            HStack {
                switch parameter.style {
                case .boolIcon:
                    Button { set(!isOn) } label: {
                        Image(systemName: InspectorIcons.symbol(forNodeIcon: parameter.styleOptions["icon"] ?? ""))
                            .frame(width: 22, height: 18)
                            .foregroundStyle(isOn ? Color.white : Color.primary)
                            .background(RoundedRectangle(cornerRadius: 5).fill(isOn ? Color.accentColor : Color.primary.opacity(0.08)))
                    }
                    .buttonStyle(.plain)
                    .help(Text(parameter.tooltip.isEmpty ? parameter.label : parameter.tooltip))
                    .accessibilityLabel(Text(parameter.label))
                    .accessibilityValue(Text(isOn ? "On" : "Off"))
                case .boolSwitch:
                    Toggle(parameter.label, isOn: binding)
                        .labelsHidden()
                        .toggleStyle(.switch)
                        .controlSize(.small)
                default:
                    Toggle(parameter.label, isOn: binding)
                        .labelsHidden()
                        .toggleStyle(.checkbox)
                }
                if parameter.labelHidden == false { Spacer(minLength: 0) }
            }
        }
    }

    private var binding: Binding<Bool> {
        Binding(get: { isOn }, set: { set($0) })
    }

    private func set(_ on: Bool) {
        model.setInt(parameter.name, component: 0, to: on ? 1 : 0)
    }
}

/// One of the options a node offers, in a menu showing their labels.
struct DropdownControl: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel

    private var current: String { parameter.strings.first ?? "" }

    /// The options, and the stored value too when it is none of them (a file edited by hand):
    /// it is shown as what it is, and not replaced by the first option.
    private var options: [InspectorOption] {
        if parameter.options.contains(where: { $0.value == current }) { return parameter.options }
        return parameter.options + [InspectorOption(value: current, label: current.isEmpty ? String(localized: "(none)") : current)]
    }

    var body: some View {
        Picker(parameter.label, selection: Binding(get: { current }, set: { model.setString(parameter.name, to: $0) })) {
            ForEach(options, id: \.value) { option in
                Text(option.label).tag(option.value)
            }
        }
        .labelsHidden()
        .pickerStyle(.menu)
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

/// A string: a field, with suggestions for an attribute name, or a code editor.
struct StringControl: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel
    let availability: RowAvailability

    private var text: String { parameter.strings.first ?? "" }

    var body: some View {
        ComponentSlot(parameter: parameter, model: model, availability: availability, component: 0) {
            switch parameter.style {
            case .code:
                CodeEditor(text: text, label: parameter.label) { model.setString(parameter.name, to: $0) }
            case .attribute:
                AttributeField(parameter: parameter, model: model)
            default:
                TextCommitField(text: text, name: parameter.label) { model.setString(parameter.name, to: $0) }
            }
        }
    }
}

/// A text field that sends what was typed when the person presses Return or leaves it.
struct TextCommitField: View {
    let text: String
    var name = ""
    let onCommit: (String) -> Void

    @State private var draft = ""
    @FocusState private var focused: Bool

    var body: some View {
        TextField("", text: $draft)
            .textFieldStyle(.roundedBorder)
            .focused($focused)
            .accessibilityLabel(Text(name))
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
        draft = text
    }
}

/// An attribute name, typed or picked from the attributes of the geometry wired into the node.
struct AttributeField: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel
    @State private var showing = false

    var body: some View {
        HStack(spacing: 2) {
            TextCommitField(text: parameter.strings.first ?? "", name: parameter.label) {
                model.setString(parameter.name, to: $0)
            }
            Button {
                // The input is asked only now: listing its attributes can mean cooking it.
                model.loadSuggestions(parameter.name)
                showing = true
            } label: {
                Image(systemName: "chevron.down.circle")
            }
            .buttonStyle(.borderless)
            .help(Text("Pick from the attributes of the input"))
            .popover(isPresented: $showing, arrowEdge: .bottom) {
                let names = model.suggestions[parameter.name] ?? []
                VStack(alignment: .leading, spacing: 2) {
                    if names.isEmpty {
                        Text("No matching attribute on the input.")
                            .foregroundStyle(.secondary)
                            .padding(8)
                    }
                    ForEach(names, id: \.self) { name in
                        Button(name) {
                            model.setString(parameter.name, to: name)
                            showing = false
                        }
                        .buttonStyle(.plain)
                        .padding(.horizontal, 8).padding(.vertical, 3)
                    }
                }
                .padding(.vertical, 4)
                .frame(minWidth: 140)
            }
        }
    }
}

/// Code, in a monospaced box, applied with the button or Command Return.
struct CodeEditor: View {
    let text: String
    let label: String
    let onApply: (String) -> Void
    @State private var draft = ""

    var body: some View {
        VStack(alignment: .trailing, spacing: 4) {
            TextEditor(text: $draft)
                .font(.system(size: 11, design: .monospaced))
                .scrollContentBackground(.hidden)
                .padding(4)
                .frame(minHeight: 110)
                .background(RoundedRectangle(cornerRadius: 5).fill(Color(nsColor: .textBackgroundColor)))
                .overlay(RoundedRectangle(cornerRadius: 5).stroke(Color.primary.opacity(0.2)))
                .accessibilityLabel(Text(label))
            Button("Apply") { onApply(draft) }
                .keyboardShortcut(.return, modifiers: .command)
                .controlSize(.small)
                .disabled(draft == text)
        }
        .onAppear { draft = text }
        .onChange(of: text) { _, shown in draft = shown }
    }
}
