import AppKit
import HelixCore
import SwiftUI

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

    var body: some Commands {
        CommandGroup(replacing: .appInfo) {
            Button("About Helix") { AboutPanel.show() }
        }
        CommandGroup(before: .toolbar) {
            Button("Frame Geometry") { document?.requestFrame() }
                .keyboardShortcut("f", modifiers: [])
                .disabled(document == nil)
        }
    }
}

/// The standard About panel, with the version of the engine and the credits.
@MainActor
enum AboutPanel {
    static func show() {
        let credits = String(localized: """
        Helix is a continuation of enzo by ParkerBritt, whose engine, node system and attribute \
        model remain its foundation. Both are released under the MIT licence.
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
