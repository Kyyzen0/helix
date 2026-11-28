import AppKit
import SwiftUI

/// What a node that reads a file shows in place of a text field: the name of the file, the folder
/// it is in, and the buttons to choose another, to see it in the Finder and to read it again.
struct FileControl: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel

    private var path: String { (parameter.strings.first ?? "").trimmingCharacters(in: .whitespacesAndNewlines) }
    private var exists: Bool { !path.isEmpty && FileManager.default.fileExists(atPath: path) }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            if path.isEmpty {
                Text("No file chosen").foregroundStyle(.secondary)
            } else {
                let display = FileDisplay(path: path)
                VStack(alignment: .leading, spacing: 1) {
                    Text(display.name)
                        .fontWeight(.medium)
                        .foregroundStyle(exists ? Color.primary : Color.red)
                        .lineLimit(1)
                        .truncationMode(.middle)
                    if !display.folder.isEmpty {
                        Text(display.folder)
                            .uiFont(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                    }
                }
                // The whole path is in the tooltip, where the line above shortens it.
                .help(Text(path))
                .accessibilityElement(children: .combine)
                .accessibilityLabel(Text("\(parameter.label): \(display.name)"))
                .accessibilityValue(Text(display.folder))
            }
            buttons
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private var buttons: some View {
        ViewThatFits(in: .horizontal) {
            HStack(spacing: 6) {
                choose
                reveal(showsTitle: true)
                reload(showsTitle: true)
            }
            // A narrow column keeps the buttons and loses their words, which the tooltips and
            // the accessibility labels still say.
            HStack(spacing: 6) {
                choose
                reveal(showsTitle: false)
                reload(showsTitle: false)
            }
        }
        .controlSize(.small)
    }

    private var choose: some View {
        Button("Choose…") { chooseAnother() }
            .help(Text("Choose another file to read"))
    }

    private func reveal(showsTitle: Bool) -> some View {
        Button { reveal() } label: {
            if showsTitle { Text("Show in Finder") } else { Image(systemName: "folder") }
        }
        .disabled(!exists)
        .help(Text("Show the file in the Finder"))
        .accessibilityLabel(Text("Show in Finder"))
    }

    private func reload(showsTitle: Bool) -> some View {
        Button { model.reloadFile() } label: {
            if showsTitle { Text("Reload") } else { Image(systemName: "arrow.clockwise") }
        }
        .disabled(!exists)
        .help(Text("Read the file again, after it was changed outside Helix"))
        .accessibilityLabel(Text("Reload"))
    }

    private func chooseAnother() {
        guard let chosen = FilePanel.choose(
            near: path, title: String(localized: "Choose File"), prompt: String(localized: "Choose"),
            message: String(localized: "Choose the 3D file this node reads. The file is not changed.")
        ) else { return }
        model.setString(parameter.name, to: chosen)
    }

    private func reveal() {
        NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: path)])
    }
}

/// The open panel of the inspector, for the files a node reads.
@MainActor
enum FilePanel {
    /// Asks for a 3D file, starting in the folder of `path` or in the nearest folder of it that
    /// is still there. Nil when the panel was cancelled.
    static func choose(near path: String, title: String, prompt: String, message: String) -> String? {
        let panel = NSOpenPanel()
        panel.title = title
        panel.prompt = prompt
        panel.message = message
        panel.allowedContentTypes = ImportFormats.contentTypes
        panel.allowsOtherFileTypes = false
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        if let folder = FileDisplay.nearestExistingFolder(of: path) { panel.directoryURL = URL(fileURLWithPath: folder) }
        guard panel.runModal() == .OK, let url = panel.url else { return nil }
        return url.path
    }
}

/// The file the node reads is not where it was: moved, renamed or deleted. It says so in the
/// words of the person, with the button that finds it again.
struct MissingFileBanner: View {
    let path: String
    let parameter: String
    @ObservedObject var model: InspectorModel

    var body: some View {
        let display = FileDisplay(path: path)
        VStack(alignment: .leading, spacing: 6) {
            HStack(alignment: .firstTextBaseline, spacing: 6) {
                Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.red)
                VStack(alignment: .leading, spacing: 2) {
                    Text("The file “\(display.name)” was moved or deleted.")
                        .uiFont(.callout)
                        .fixedSize(horizontal: false, vertical: true)
                    if !display.folder.isEmpty {
                        Text("It was in \(display.folder)")
                            .uiFont(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(2)
                            .truncationMode(.middle)
                            .help(Text(path))
                    }
                }
                Spacer(minLength: 0)
            }
            Button("Locate…") { locate() }
                .controlSize(.small)
        }
        .padding(8)
        .background(RoundedRectangle(cornerRadius: 7).fill(Color.red.opacity(0.14)))
        .overlay(RoundedRectangle(cornerRadius: 7).stroke(Color.red.opacity(0.45)))
        .accessibilityElement(children: .contain)
    }

    private func locate() {
        guard let chosen = FilePanel.choose(
            near: path, title: String(localized: "Locate File"), prompt: String(localized: "Locate"),
            message: String(localized: "Choose the file “\(FileDisplay(path: path).name)” again. The scene will read it from its new place.")
        ) else { return }
        model.setString(parameter, to: chosen)
    }
}

/// The pictures of the model that were not found: which, and where they were looked for.
struct MissingTexturesBanner: View {
    let textures: [MissingTexture]

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(alignment: .firstTextBaseline, spacing: 6) {
                Image(systemName: "exclamationmark.circle.fill").foregroundStyle(.orange)
                Text("Some textures of this model were not found. It is shown without them.")
                    .uiFont(.callout)
                    .fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: 0)
            }
            ForEach(textures, id: \.name) { texture in
                VStack(alignment: .leading, spacing: 1) {
                    Text(texture.name).uiFont(.callout).fontWeight(.medium).lineLimit(1).truncationMode(.middle)
                    if let folder = texture.folder {
                        Text("Looked for in \(FileDisplay.shortened(folder))")
                            .uiFont(.caption).foregroundStyle(.secondary).lineLimit(1).truncationMode(.middle)
                            .help(Text(folder))
                    } else if let detail = texture.detail {
                        Text(detail).uiFont(.caption).foregroundStyle(.secondary).lineLimit(2)
                    }
                }
                .padding(.leading, 20)
            }
        }
        .padding(8)
        .background(RoundedRectangle(cornerRadius: 7).fill(Color.orange.opacity(0.14)))
        .overlay(RoundedRectangle(cornerRadius: 7).stroke(Color.orange.opacity(0.45)))
        .accessibilityElement(children: .contain)
    }
}
