import SwiftUI

/// The submenus of the Add menu: one per group of node types, each listing its nodes with their
/// icons. The menu bar, the toolbar and the context menu of the canvas all show these.
struct AddNodeSubmenus: View {
    let types: [EditorNodeType]
    let add: (EditorNodeType) -> Void

    var body: some View {
        ForEach(NodeCategory.groups(of: types), id: \.category) { group in
            Menu {
                NodeList(types: group.types, add: add)
            } label: {
                Label(group.category.title, systemImage: group.category.symbol)
            }
        }
    }
}

/// The nodes of one group, as menu items.
struct NodeList: View {
    let types: [EditorNodeType]
    let add: (EditorNodeType) -> Void

    var body: some View {
        ForEach(types, id: \.name) { type in
            Button { add(type) } label: {
                Label(type.label, systemImage: NodeCategory.symbol(of: type))
            }
        }
    }
}

/// The shelf along the top of the network pane: a button for each group of node types, which
/// opens a menu of them, one for the import of a 3D model, and one for the search palette. It is
/// where a person finds out what can be made.
struct NodeShelf: View {
    @ObservedObject var model: NodeEditorModel
    let importModel: () -> Void
    @Environment(\.interfaceSize) private var size

    var body: some View {
        ViewThatFits(in: .horizontal) {
            shelf(showsTitles: true)
            shelf(showsTitles: false)
        }
    }

    private func shelf(showsTitles: Bool) -> some View {
        HStack(spacing: size.points(4)) {
            ForEach(NodeCategory.groups(of: model.paletteTypes), id: \.category) { group in
                Menu {
                    NodeList(types: group.types) { model.addNode($0) }
                } label: {
                    if showsTitles {
                        Label(group.category.title, systemImage: group.category.symbol)
                    } else {
                        Image(systemName: group.category.symbol)
                    }
                }
                .help(Text("Add a node: \(group.category.title)"))
                .accessibilityLabel(Text(group.category.title))
            }
            Button(action: importModel) {
                if showsTitles {
                    Label("Import", systemImage: "square.and.arrow.down")
                } else {
                    Image(systemName: "square.and.arrow.down")
                }
            }
            .help(Text("Import a 3D model (⌘I)"))
            .accessibilityLabel(Text("Import"))
            Button { model.openPalette() } label: {
                if showsTitles {
                    Label("Search", systemImage: "magnifyingglass")
                } else {
                    Image(systemName: "magnifyingglass")
                }
            }
            .help(Text("Search every node (⌘K or Tab)"))
            .accessibilityLabel(Text("Search"))
        }
        .menuStyle(.button)
        .menuIndicator(.hidden)
        .buttonStyle(.bordered)
        .fixedSize()
    }
}
