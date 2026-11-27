import AppKit
import SwiftUI

/// The window of Help > Getting Started: what Helix is about, in a few short parts.
struct GettingStartedView: View {
    @Environment(\.interfaceSize) private var size
    @Environment(\.theme) private var theme
    /// The window scrolls; a picture of all of it does not.
    var scrolls = true

    /// One part: an icon, a title and a few sentences.
    private struct Part: Identifiable {
        let icon: String
        let title: String
        let text: String
        var id: String { icon }
    }

    private var sections: [Part] {
        [
            Part(
                icon: "plus.square",
                title: String(localized: "Create something"),
                text: String(localized: "Open the Add menu, or click a button of the shelf under the 3D view, to make a node: a cube, a grid, a sphere. On an empty scene you can also start from an example.")
            ),
            Part(
                icon: "square.and.arrow.down",
                title: String(localized: "Import a model"),
                text: String(localized: "Choose Add ▸ Import a 3D Model (⌘I), or drop a file on the window. Helix reads glTF, FBX, OBJ and more. The file itself is never changed.")
            ),
            Part(
                icon: "cube.transparent",
                title: String(localized: "Move around the 3D view"),
                text: String(localized: "Drag to orbit, hold ⌥ to pan, scroll with ⌘ or pinch to zoom, and press F to frame what you made. The card at the bottom left of the view says it again; Help ▸ Show Navigation Help brings it back.")
            ),
            Part(
                icon: "point.3.connected.trianglepath.dotted",
                title: String(localized: "Nodes"),
                text: String(localized: "A node is one step. Connect the output at the bottom of a node to the input at the top of the next. The flag marks the node the 3D view shows: double-click a node to show it. A node in error has a red outline and a warning triangle; select it to read why.")
            ),
            Part(
                icon: "slider.horizontal.3",
                title: String(localized: "The inspector"),
                text: String(localized: "Select a node to set its parameters on the right. Drag a slider or type a number. Right-click a parameter for expressions and keyframes.")
            ),
            Part(
                icon: "play.rectangle",
                title: String(localized: "The timeline"),
                text: String(localized: "Press the space bar to play. Right-click a parameter to key it: the diamonds on the ruler are its keys, and the line with a small triangle is the current frame.")
            ),
            Part(
                icon: "square.and.arrow.down.on.square",
                title: String(localized: "Save"),
                text: String(localized: "⌘S saves the scene as a .kut file. A 3D file you open becomes a new scene that holds the model, and saving asks where to put it: the model itself is never written to.")
            ),
        ]
    }

    var body: some View {
        if scrolls { ScrollView { content } } else { content }
    }

    private var content: some View {
            VStack(alignment: .leading, spacing: size.points(18)) {
                HStack(spacing: size.points(12)) {
                    Image(nsImage: NSApplication.shared.applicationIconImage)
                        .resizable()
                        .interpolation(.high)
                        .frame(width: size.points(44), height: size.points(44))
                        .accessibilityHidden(true)
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Getting Started")
                            .uiFont(.title3, weight: .semibold)
                        Text("Helix builds shapes from steps you connect, called nodes.")
                            .uiFont(.body)
                            .foregroundStyle(.secondary)
                    }
                }
                ForEach(sections) { section in
                    HStack(alignment: .top, spacing: size.points(12)) {
                        Image(systemName: section.icon)
                            .font(.system(size: size.points(18)))
                            .foregroundStyle(theme.accent)
                            .frame(width: size.points(28))
                            .accessibilityHidden(true)
                        VStack(alignment: .leading, spacing: size.points(3)) {
                            Text(verbatim: section.title)
                                .uiFont(.headline)
                            Text(verbatim: section.text)
                                .uiFont(.body)
                                .foregroundStyle(.secondary)
                                .fixedSize(horizontal: false, vertical: true)
                        }
                    }
                    .accessibilityElement(children: .combine)
                }
            }
            .padding(size.points(24))
            .frame(maxWidth: .infinity, alignment: .leading)
    }
}
