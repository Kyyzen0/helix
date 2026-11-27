import AppKit
import HelixCore
import SwiftUI
import UniformTypeIdentifiers

struct HelixApp: App {
    init() {
        // The appearance of the theme chosen last time, before the first window opens.
        ThemeAppearance.applyStored()
    }

    var body: some Scene {
        DocumentGroup(newDocument: { HelixDocument() }) { file in
            DocumentView(document: file.document)
                .focusedSceneObject(file.document)
                .themed()
        }
        .commands { HelixCommands() }
        // ⌘, opens it.
        Settings {
            SettingsView()
                .themed()
        }
    }
}

/// The menu items the document group does not give: the viewport, and the About panel.
struct HelixCommands: Commands {
    @FocusedObject private var document: HelixDocument?
    @AppStorage(WireframeSetting.defaultsKey) private var wireframe = WireframeSetting.auto
    @AppStorage(MaterialsSetting.defaultsKey) private var materialsVisible = true
    @AppStorage(InspectorSetting.defaultsKey) private var inspectorShown = true
    @AppStorage(TimelineSetting.shownKey) private var timelineShown = true
    @AppStorage(TimelineSetting.loopKey) private var loop = true
    @AppStorage(TimelineSetting.everyFrameKey) private var everyFrame = false
    @AppStorage(TimelineSetting.lowerPaneKey) private var lowerPane = TimelineSetting.LowerPane.network.rawValue

    var body: some Commands {
        CommandGroup(replacing: .appInfo) {
            Button("About Helix") { AboutPanel.show() }
        }
        CommandGroup(after: .newItem) {
            Button("Import…") { document?.chooseFileToImport() }
                .keyboardShortcut("i", modifiers: .command)
                .disabled(document == nil || document?.isActive != true)
        }
        // The keys F, shift W and shift M belong to the viewport, which handles them itself
        // while it has the keys. A menu shortcut without the command key would also fire while a
        // text field has them, and typing an "M" in the palette must not hide the materials.
        CommandGroup(before: .toolbar) {
            Button("Frame Geometry") { document?.requestFrame() }
                .disabled(document == nil)
            Picker("Wireframe", selection: wireframeBinding) {
                ForEach(WireframeSetting.allCases) { setting in
                    Text(setting.title).tag(setting)
                }
            }
            Button("Cycle Wireframe") { wireframeBinding.wrappedValue = wireframe.next }
            Toggle("Materials", isOn: materialsBinding)
            // Command and option keep the shortcut out of the way of text fields, which take
            // the plain keys.
            Button(inspectorShown ? "Hide Inspector" : "Show Inspector") { inspectorShown.toggle() }
                .keyboardShortcut("i", modifiers: [.command, .option])
                .disabled(document == nil)
            Button(timelineShown ? "Hide Timeline" : "Show Timeline") { timelineShown.toggle() }
                .keyboardShortcut("t", modifiers: [.command, .option])
                .disabled(document == nil)
            Button(spreadsheetShown ? "Show Node Editor" : "Show Spreadsheet") {
                lowerPane = (spreadsheetShown ? TimelineSetting.LowerPane.network : .spreadsheet).rawValue
            }
            .keyboardShortcut("s", modifiers: [.command, .option])
            .disabled(document == nil)
        }
        // The keys of playback (space, the arrows) belong to the views that have the keyboard, for
        // the reason the viewport keys do. These items have none, and do the same.
        CommandMenu("Timeline") {
            Button("Play/Pause") { document?.timeline.perform(.playPause) }
                .disabled(document == nil || document?.isActive != true)
            Button("Step Back") { document?.timeline.perform(.stepBack) }
                .disabled(document == nil || document?.isActive != true)
            Button("Step Forward") { document?.timeline.perform(.stepForward) }
                .disabled(document == nil || document?.isActive != true)
            Button("Go to Start") { document?.timeline.perform(.goToStart) }
                .disabled(document == nil || document?.isActive != true)
            Button("Go to End") { document?.timeline.perform(.goToEnd) }
                .disabled(document == nil || document?.isActive != true)
            Divider()
            Toggle("Loop", isOn: $loop)
            Toggle("Play Every Frame", isOn: $everyFrame)
            Divider()
            Button("Clear Cook Cache") { document?.clearCookCache() }
                .disabled(document == nil || document?.isActive != true)
        }
        CommandMenu("Node") {
            Button("Add Node…") { document?.editor.openPalette() }
                .keyboardShortcut("k", modifiers: .command)
                .disabled(document == nil || document?.isActive != true)
            Button("Frame Nodes") { document?.editor.frameSelection() }
                .disabled(document == nil || document?.isActive != true)
        }
    }
}

extension HelixCommands {
    private var spreadsheetShown: Bool { lowerPane == TimelineSetting.LowerPane.spreadsheet.rawValue }
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
