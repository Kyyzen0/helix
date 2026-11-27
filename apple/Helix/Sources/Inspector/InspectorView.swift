import SwiftUI

/// The inspector column: the node selected, and its parameters in the order the node declares.
struct InspectorView: View {
    @ObservedObject var model: InspectorModel
    /// Whether the rows scroll. A picture of the panel draws them all at their full height.
    var scrolls = true

    var body: some View {
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
            } else {
                rows
            }
        }
    }
}

/// The name of the node, its type, the display flag and what went wrong when it cooked.
struct InspectorHeader: View {
    let node: InspectorNode
    @ObservedObject var model: InspectorModel

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .firstTextBaseline) {
                VStack(alignment: .leading, spacing: 1) {
                    Text(node.name).font(.headline).lineLimit(1)
                    Text(node.typeLabel).font(.caption).foregroundStyle(.secondary).lineLimit(1)
                }
                Spacer()
                Button { model.setDisplay() } label: {
                    Image(systemName: node.isDisplay ? "flag.fill" : "flag")
                        .foregroundStyle(node.isDisplay ? Color.accentColor : Color.secondary)
                }
                .buttonStyle(.borderless)
                .help(Text(node.isDisplay ? "This node is shown in the viewport" : "Show this node in the viewport"))
                .accessibilityLabel(Text("Display node"))
                .accessibilityValue(Text(node.isDisplay ? "On" : "Off"))
            }
            if let error = node.error { DiagnosticBanner(text: error, isError: true) }
            ForEach(node.warnings, id: \.self) { DiagnosticBanner(text: $0, isError: false) }
            if let message = model.message {
                DiagnosticBanner(text: message, isError: true)
            }
        }
        .padding(12)
    }
}

/// An error in red, or a warning in yellow.
struct DiagnosticBanner: View {
    let text: String
    let isError: Bool

    var body: some View {
        let tint: Color = isError ? .red : .orange
        HStack(alignment: .firstTextBaseline, spacing: 6) {
            Image(systemName: isError ? "exclamationmark.octagon.fill" : "exclamationmark.triangle.fill")
                .foregroundStyle(tint)
            Text(text)
                .font(.callout)
                .fixedSize(horizontal: false, vertical: true)
                .textSelection(.enabled)
            Spacer(minLength: 0)
        }
        .padding(8)
        .background(RoundedRectangle(cornerRadius: 7).fill(tint.opacity(0.14)))
        .overlay(RoundedRectangle(cornerRadius: 7).stroke(tint.opacity(0.45)))
        .accessibilityElement(children: .combine)
        .accessibilityLabel(Text(isError ? "Error: \(text)" : "Warning: \(text)"))
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
                Text(label).font(.subheadline.weight(.semibold))
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
