import SwiftUI

/// The inspector column: the node selected, and its parameters in the order the node declares.
struct InspectorView: View {
    @ObservedObject var model: InspectorModel
    /// Whether the rows scroll. A picture of the panel draws them all at their full height.
    var scrolls = true
    @Environment(\.interfaceSize) private var size
    @Environment(\.theme) private var theme

    var body: some View {
        content
            .font(.system(size: size.points(13)))
            // The column takes the colour and the appearance of the theme, not the material of the system.
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(theme.window)
            .environment(\.colorScheme, theme.palette.isDark ? .dark : .light)
            .foregroundStyle(Color(theme.palette.text))
    }

    @ViewBuilder
    private var content: some View {
        switch model.content {
        case .none:
            InspectorPlaceholder(text: String(localized: "No node selected"), symbol: "slider.horizontal.3")
        case .many(let count):
            InspectorPlaceholder(text: String(localized: "\(count) nodes selected"), symbol: "square.stack.3d.up")
        case .node(let node):
            InspectorNodeView(node: node, model: model, scrolls: scrolls)
        }
    }
}

struct InspectorPlaceholder: View {
    let text: String
    let symbol: String

    var body: some View {
        VStack(spacing: 8) {
            Image(systemName: symbol).font(.system(size: 28)).foregroundStyle(.tertiary)
            Text(text).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .accessibilityElement(children: .combine)
    }
}

struct InspectorNodeView: View {
    let node: InspectorNode
    @ObservedObject var model: InspectorModel
    var scrolls = true

    private var rows: some View {
        VStack(alignment: .leading, spacing: 7) {
            if let skeleton = node.skeleton { AnimationSection(skeleton: skeleton, model: model) }
            ForEach(model.entries) { entry in
                EntryView(entry: entry, model: model, nested: false)
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    var body: some View {
        VStack(spacing: 0) {
            InspectorHeader(node: node, model: model)
            Divider()
            if scrolls {
                ScrollView { rows }
                    .scrollContentBackground(.hidden)
            } else {
                rows
            }
        }
    }
}

/// The name of the node, its type, the display flag and what went wrong when it cooked.
struct InspectorHeader: View {
    @Environment(\.theme) private var theme
    let node: InspectorNode
    @ObservedObject var model: InspectorModel

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .firstTextBaseline) {
                VStack(alignment: .leading, spacing: 1) {
                    InspectorNameField(node: node, model: model)
                    Text(node.typeLabel).uiFont(.caption).foregroundStyle(.secondary).lineLimit(1)
                    if let basedOn = node.basedOn {
                        Text("based on \(basedOn)").uiFont(.caption).foregroundStyle(.tertiary).lineLimit(1)
                    }
                }
                Spacer()
                Button { model.setDisplay() } label: {
                    Image(systemName: node.isDisplay ? "flag.fill" : "flag")
                        .foregroundStyle(node.isDisplay ? theme.displayFlag : Color.secondary)
                }
                .buttonStyle(.borderless)
                .help(node.isDisplay ? Text("This node is shown in the viewport") : Text("Show this node in the viewport"))
                .accessibilityLabel(Text("Display node"))
                .accessibilityValue(node.isDisplay ? Text("On") : Text("Off"))
            }
            if let attribute = node.writtenAttribute { AttributeNote(attribute: attribute, model: model) }
            if let missing = node.missingFile, let parameter = node.fileParameters.first {
                // The engine's own error says the same thing, in English and with a path: the
                // banner replaces it, and has the button.
                MissingFileBanner(path: missing, parameter: parameter.name, model: model)
            } else if let error = node.error {
                DiagnosticBanner(text: error, isError: true)
            }
            if !node.missingTextures.isEmpty { MissingTexturesBanner(textures: node.missingTextures) }
            ForEach(node.warnings, id: \.self) { DiagnosticBanner(text: $0, isError: false) }
            if let message = model.message {
                DiagnosticBanner(text: message, isError: true)
            }
        }
        .padding(12)
    }
}

/// What a node that writes data without moving the shape says: what it writes, and a button to
/// see it in the spreadsheet, since nothing in the 3D view shows it.
struct AttributeNote: View {
    let attribute: String
    @ObservedObject var model: InspectorModel

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(alignment: .firstTextBaseline, spacing: 6) {
                Image(systemName: "info.circle.fill").foregroundStyle(.secondary)
                Text("This node adds the data “\(attribute)” without changing the shape.")
                    .uiFont(.callout)
                    .fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: 0)
            }
            if model.showSpreadsheet != nil {
                Button("Show in Spreadsheet") { model.showSpreadsheet?() }
                    .controlSize(.small)
            }
        }
        .padding(8)
        .background(RoundedRectangle(cornerRadius: 7).fill(Color.secondary.opacity(0.12)))
        .overlay(RoundedRectangle(cornerRadius: 7).stroke(Color.secondary.opacity(0.35)))
        .accessibilityElement(children: .contain)
    }
}

/// The name of the node in the header, which a click or Return changes. Return takes the new name,
/// Escape leaves the old one, and a name that cannot be taken stays in the field with the reason
/// under it.
struct InspectorNameField: View {
    let node: InspectorNode
    @ObservedObject var model: InspectorModel
    @FocusState private var focused: Bool

    var body: some View {
        if let draft = model.renameDraft {
            VStack(alignment: .leading, spacing: 3) {
                TextField("Name", text: Binding(get: { draft }, set: {
                    model.renameDraft = $0
                    model.renameDraftChanged()
                }))
                .textFieldStyle(.roundedBorder)
                .uiFont(.headline)
                .focused($focused)
                .onSubmit { model.commitRename() }
                .onExitCommand { model.cancelRename() }
                .onAppear { focused = true }
                .accessibilityLabel(Text("Node name"))
                if let error = model.renameError {
                    Text(error)
                        .uiFont(.caption)
                        .foregroundStyle(.red)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
        } else {
            Button { model.beginRename() } label: {
                Text(node.name).uiFont(.headline).lineLimit(1)
            }
            .buttonStyle(.plain)
            .help("Click to rename the node")
            .accessibilityLabel(Text("Node name: \(node.name)"))
            .accessibilityHint(Text("Renames the node"))
        }
    }
}

/// An error in red with a triangle, or a warning in orange with a circle: the shape tells them
/// apart as the colour does.
struct DiagnosticBanner: View {
    let text: String
    let isError: Bool

    var body: some View {
        let tint: Color = isError ? .red : .orange
        HStack(alignment: .firstTextBaseline, spacing: 6) {
            Image(systemName: isError ? "exclamationmark.triangle.fill" : "exclamationmark.circle.fill")
                .foregroundStyle(tint)
            Text(text)
                .uiFont(.callout)
                .fixedSize(horizontal: false, vertical: true)
                .textSelection(.enabled)
            Spacer(minLength: 0)
        }
        .padding(8)
        .background(RoundedRectangle(cornerRadius: 7).fill(tint.opacity(0.14)))
        .overlay(RoundedRectangle(cornerRadius: 7).stroke(tint.opacity(0.45)))
        .accessibilityElement(children: .combine)
        .accessibilityLabel(isError ? Text("Error: \(text)") : Text("Warning: \(text)"))
    }
}

/// A row, a group of rows or a heading.
struct EntryView: View {
    let entry: InspectorEntry
    @ObservedObject var model: InspectorModel
    let nested: Bool

    var body: some View {
        switch entry {
        case .parameter(let parameter):
            ParameterRow(parameter: parameter, model: model, inline: nested)
        case .divider(_, let label, let icon):
            SectionHeading(label: label, icon: icon)
        case .group(let group):
            GroupView(group: group, model: model)
        }
    }
}

struct SectionHeading: View {
    let label: String
    let icon: String

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Divider()
            HStack(spacing: 5) {
                if !icon.isEmpty {
                    Image(systemName: InspectorIcons.symbol(forNodeIcon: icon)).foregroundStyle(.secondary)
                }
                Text(label).uiFont(.subheadline, weight: .semibold)
            }
        }
        .padding(.top, 6)
        .accessibilityAddTraits(.isHeader)
    }
}

/// A group of rows. One laid out side by side is a single row: its label, then its members.
/// One laid out vertically opens and closes.
struct GroupView: View {
    let group: InspectorGroup
    @ObservedObject var model: InspectorModel
    @State private var expanded = true

    var body: some View {
        Group {
            if group.horizontal {
                HStack(alignment: .firstTextBaseline, spacing: 6) {
                    if !group.label.isEmpty {
                        Text(group.label)
                            .foregroundStyle(.secondary)
                            .lineLimit(2)
                            .fixedSize(horizontal: false, vertical: true)
                            .frame(width: ParameterLabel.width, alignment: .leading)
                    }
                    ForEach(flattened) { entry in
                        EntryView(entry: entry, model: model, nested: true)
                    }
                    Spacer(minLength: 0)
                }
            } else {
                DisclosureGroup(group.label, isExpanded: $expanded) {
                    VStack(alignment: .leading, spacing: 7) {
                        ForEach(group.children) { EntryView(entry: $0, model: model, nested: false) }
                    }
                    .padding(.top, 4)
                }
            }
        }
        .opacity(group.enabled ? 1 : 0.4)
        .disabled(!group.enabled)
    }

    /// The members of a row group, with the groups inside it opened out into the same row.
    private var flattened: [InspectorEntry] {
        group.children.flatMap { entry -> [InspectorEntry] in
            if case .group(let inner) = entry, inner.horizontal { return InspectorGroupFlattening.members(of: inner) }
            return [entry]
        }
    }
}

enum InspectorGroupFlattening {
    static func members(of group: InspectorGroup) -> [InspectorEntry] {
        group.children.flatMap { entry -> [InspectorEntry] in
            if case .group(let inner) = entry, inner.horizontal { return members(of: inner) }
            return [entry]
        }
    }
}
