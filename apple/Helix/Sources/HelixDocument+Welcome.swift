import AppKit
import HelixCore
import SwiftUI

/// What the welcome screen of an empty scene does, and what a new node or file does to the view.
extension HelixDocument {
    /// Builds an example into the scene, shows it, and selects its last node so the inspector
    /// has something to show. One step of undo.
    func addStarter(_ example: StarterExample) {
        let outcome = performEdit(named: String(localized: "Add Example")) {
            example.build().map { .failed($0) } ?? .done
        }
        if let error = outcome.error {
            presentedError = error
            return
        }
        showAndSelectDisplayNode()
    }

    /// Frames the whole graph in the editor and the geometry in the viewport, and selects the node
    /// the viewport shows, which is the one that was just made or imported.
    func showAndSelectDisplayNode() {
        refreshEditor(frame: true)
        let display = helix.core.displayNode()
        if display != 0 { editor.select(nodes: [display]) }
        viewport.requestFrame()
    }

    /// Opens the Open panel of the app, for a scene or a 3D file.
    func chooseSceneToOpen() {
        NSDocumentController.shared.openDocument(nil)
    }
}

extension HelixDocument {
    /// With `HELIX_DEBUG_LAUNCH=1`, says how large the window came up against the screen it is on
    /// ("helix-launch: window 1632x1013 of 1920x1055"), which `Scripts/launch-smoke.py` reads.
    static func logWindowSize() {
        guard ProcessInfo.processInfo.environment["HELIX_DEBUG_LAUNCH"] == "1" else { return }
        Task { @MainActor in
            try? await Task.sleep(for: .seconds(1.5))
            guard let window = NSApplication.shared.windows.first(where: { $0.isVisible && $0.contentViewController != nil }),
                  let screen = window.screen else { return }
            let frame = window.frame, visible = screen.visibleFrame
            FileHandle.standardError.write(Data( // l10n-ok: read by launch-smoke.py
                "helix-launch: window \(Int(frame.width))x\(Int(frame.height)) of \(Int(visible.width))x\(Int(visible.height))\n".utf8))
        }
    }
}
