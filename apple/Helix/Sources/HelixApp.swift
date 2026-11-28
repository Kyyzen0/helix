import AppKit
import HelixCore
import SwiftUI
import UniformTypeIdentifiers

struct HelixApp: App {
    init() {
        // A launch with nothing to open makes a new scene, as most document apps do, and not the
        // Open panel AppKit shows by default.
        UserDefaults.standard.register(defaults: [
            "NSShowAppCentricOpenPanelInsteadOfUntitledFile": false,
        ])
        // The old on/off setting of the timeline becomes its mode.
        TimelineSetting.migrateLegacy()
        // The appearance of the theme chosen last time, before the first window opens. Not here
        // and now: reading NSApplication.shared this early makes the application object before
        // SwiftUI does, and SwiftUI then crashes setting up its document classes at launch. The
        // will-finish-launching notice comes once SwiftUI has made it, and before any window.
        NotificationCenter.default.addObserver(
            forName: NSApplication.willFinishLaunchingNotification, object: nil, queue: .main
        ) { _ in
            MainActor.assumeIsolated { ThemeAppearance.applyStored() }
        }
        // AI agents reach the open scene over a socket, from the moment the app is up until it quits.
        NotificationCenter.default.addObserver(
            forName: NSApplication.didFinishLaunchingNotification, object: nil, queue: .main
        ) { _ in
            MainActor.assumeIsolated {
                AgentLink.shared.start()
            }
        }
        NotificationCenter.default.addObserver(
            forName: NSApplication.willTerminateNotification, object: nil, queue: .main
        ) { _ in
            MainActor.assumeIsolated { AgentLink.shared.stop() }
        }
    }

    var body: some Scene {
        DocumentGroup(newDocument: { HelixDocument() }) { file in
            DocumentView(document: file.document, fileURL: file.fileURL)
                .focusedSceneObject(file.document)
                .themed()
                .sized()
        }
        .defaultWindowPlacement { _, context in
            WindowPlacement.helixDefault(in: context.defaultDisplay.visibleRect)
        }
        .commands { HelixCommands() }
        // Help > Getting Started.
        Window("Getting Started", id: GettingStartedWindow.id) {
            GettingStartedView()
                .frame(minWidth: 460, minHeight: 360)
                .themed()
                .sized()
        }
        .defaultSize(width: 580, height: 700)
        .windowResizability(.contentMinSize)
        // ⌘, opens it.
        Settings {
            SettingsView()
                .themed()
                .sized()
        }
    }
}

/// The window of Help > Getting Started.
enum GettingStartedWindow {
    static let id = "getting-started" // l10n-ok: the identifier of a window
}

/// The menu items the document group does not give: the viewport, and the About panel.
struct HelixCommands: Commands {
    @FocusedObject private var document: HelixDocument?
    @Environment(\.openWindow) private var openWindow
    @AppStorage(NavigationLegendSetting.hiddenKey) private var navigationLegendHidden = false
    @AppStorage(InspectorSetting.defaultsKey) private var inspectorShown = true
    @AppStorage(TimelineSetting.modeKey) private var timelineMode = TimelineMode.automatic
    @AppStorage(TimelineSetting.loopKey) private var loop = true
    @AppStorage(TimelineSetting.everyFrameKey) private var everyFrame = false
    @AppStorage(TimelineSetting.lowerPaneKey) private var lowerPane = TimelineSetting.LowerPane.network.rawValue

    var body: some Commands {
        CommandGroup(replacing: .appInfo) {
            Button("About Helix") { AboutPanel.show() }
        }
        // The Help menu of macOS has nothing of Helix's in it: a guide, and the legend of the viewport.
        CommandGroup(replacing: .help) {
            Button("Getting Started") { openWindow(id: GettingStartedWindow.id) }
            Button("Show Navigation Help") { navigationLegendHidden = false }
        }
        // The camera, the frame, the wireframe, the materials, the background and the layouts. F, shift
        // W and shift M are on the items: AppKit keeps a key without a command key from the menu
        // while a text field is being edited, so typing in the palette or the inspector is safe.
        CommandGroup(before: .toolbar) {
            ViewMenuContent(document: document, shortcuts: true)
            Divider()
            // Command and option keep the shortcut out of the way of text fields, which take
            // the plain keys.
            Button(inspectorShown ? LocalizedStringKey("Hide Inspector") : LocalizedStringKey("Show Inspector")) { inspectorShown.toggle() }
                .keyboardShortcut("i", modifiers: [.command, .option])
                .disabled(document == nil)
            // The shortcut shows or hides the bar for good (Always or Never), whatever the scene
            // holds; the submenu lets the person go back to Automatic.
            let timelineShown = timelineMode.isShown(sceneAnimates: document?.sceneAnimates ?? false)
            Button(timelineShown ? LocalizedStringKey("Hide Timeline") : LocalizedStringKey("Show Timeline")) {
                timelineMode = timelineShown ? .never : .always
            }
            .keyboardShortcut("t", modifiers: [.command, .option])
            .disabled(document == nil)
            Menu("Timeline Bar") {
                Picker("Timeline Bar", selection: $timelineMode) {
                    ForEach(TimelineMode.allCases) { mode in Text(mode.title).tag(mode) }
                }
                .pickerStyle(.inline)
                .labelsHidden()
            }
            Button(spreadsheetShown ? LocalizedStringKey("Show Node Editor") : LocalizedStringKey("Show Spreadsheet")) {
                lowerPane = (spreadsheetShown ? TimelineSetting.LowerPane.network : .spreadsheet).rawValue
            }
            .keyboardShortcut("s", modifiers: [.command, .option])
            .disabled(document == nil)
        }
        // Importing a model is a way to bring a file in, which a Mac keeps in the File menu.
        CommandGroup(replacing: .importExport) {
            Button("Import a 3D Model…") { document?.chooseFileToImport() }
                .keyboardShortcut("i", modifiers: .command)
                .disabled(document == nil || document?.isActive != true)
            Divider()
            // What the work comes out as: a picture of the viewport, and the geometry it shows.
            Button("Export Image…") { document?.exportImage() }
                .keyboardShortcut("e", modifiers: [.command, .shift])
                .disabled(document?.canExport != true)
            Button("Export Geometry…") { document?.exportGeometry() }
                .disabled(document?.canExport != true)
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
        // The nodes: adding one and what is done to the ones selected. The commands that work on
        // the selection are greyed when nothing is selected, and the key of the editor (H) is
        // shown, as the menu of the View does for its own.
        CommandMenu("Node") {
            NodeMenuContent(document: document)
        }
    }
}

extension HelixCommands {
    private var spreadsheetShown: Bool { lowerPane == TimelineSetting.LowerPane.spreadsheet.rawValue }
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
