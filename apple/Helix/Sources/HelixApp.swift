import AppKit
import HelixCore
import SwiftUI
import UniformTypeIdentifiers

struct HelixApp: App {
    var body: some Scene {
        DocumentGroup(newDocument: { HelixDocument() }) { file in
            DocumentView(document: file.document)
                .focusedSceneObject(file.document)
        }
        .commands { HelixCommands() }
    }
}

/// The menu items the document group does not give: the viewport, and the About panel.
struct HelixCommands: Commands {
    @FocusedObject private var document: HelixDocument?
    @AppStorage(WireframeSetting.defaultsKey) private var wireframe = WireframeSetting.auto
    @AppStorage(MaterialsSetting.defaultsKey) private var materialsVisible = true

    var body: some Commands {
        CommandGroup(replacing: .appInfo) {
            Button("About Helix") { AboutPanel.show() }
        }
        CommandGroup(after: .newItem) {
            Button("Import…") { document?.chooseFileToImport() }
                .keyboardShortcut("i", modifiers: .command)
                .disabled(document == nil || document?.isActive != true)
        }
        CommandGroup(before: .toolbar) {
            Button("Frame Geometry") { document?.requestFrame() }
                .keyboardShortcut("f", modifiers: [])
                .disabled(document == nil)
            Picker("Wireframe", selection: wireframeBinding) {
                ForEach(WireframeSetting.allCases) { setting in
                    Text(setting.title).tag(setting)
                }
            }
            Button("Cycle Wireframe") { wireframeBinding.wrappedValue = wireframe.next }
                .keyboardShortcut("w", modifiers: .shift)
            Toggle("Materials", isOn: materialsBinding)
                .keyboardShortcut("m", modifiers: .shift)
        }
    }
}

extension HelixCommands {
    /// Sets the wireframe mode and asks the viewport of the window in front to draw again.
    private var wireframeBinding: Binding<WireframeSetting> {
        Binding(
            get: { wireframe },
            set: { setting in
                wireframe = setting
                document?.viewport.redraw()
            }
        )
    }
}

extension HelixCommands {
    /// Shows or hides the materials and asks the viewport of the window in front to draw again.
    private var materialsBinding: Binding<Bool> {
        Binding(
            get: { materialsVisible },
            set: { visible in
                materialsVisible = visible
                document?.viewport.redraw()
            }
        )
    }
}

/// The standard About panel, with the version of the engine and the credits.
@MainActor
enum AboutPanel {
    static func show() {
        let credits = String(localized: """
        Helix is a continuation of enzo by ParkerBritt, whose engine, node system and attribute \
        model remain its foundation. Both are released under the MIT licence.

        3D files are read with Assimp (BSD 3-Clause, with Draco, Apache 2.0) and Model I/O.
        """)
        NSApplication.shared.orderFrontStandardAboutPanel(options: [
            .applicationVersion: String(engine: helix.core.version()),
            .version: Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "",
            .credits: NSAttributedString(
                string: credits,
                attributes: [.font: NSFont.systemFont(ofSize: NSFont.smallSystemFontSize)]
            ),
        ])
    }
}
