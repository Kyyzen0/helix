import AppKit
import Foundation

/// A debug aid for `Scripts/launch-smoke.py`, off unless asked for in the environment.
///
/// With `HELIX_DEBUG_IMPORT_AFTER_LAUNCH=<path to a 3D file>`, once the untitled scene is shown,
/// the file is imported the way the Import button does it (`importFile`, one step of undo, so the
/// document becomes edited), then the real autosave is made to run (`autosave(withImplicitCancellability:)`) and,
/// with `HELIX_DEBUG_SAVE_TO=<path of a .kut>`, an explicit save to that path follows. What
/// happens is written to standard error as "helix-launch: import-after-launch ..." lines.
extension HelixDocument {
    func scheduleDebugImportAfterLaunch() {
        let environment = ProcessInfo.processInfo.environment
        guard let path = environment["HELIX_DEBUG_IMPORT_AFTER_LAUNCH"], !path.isEmpty else { return }
        let saveTo = environment["HELIX_DEBUG_SAVE_TO"]
        Task { @MainActor [weak self] in
            try? await Task.sleep(for: .seconds(3))
            guard let self, self.isActive else { return }
            let error = self.importFile(URL(fileURLWithPath: path))
            Self.debugLog("import-after-launch imported error=\(error ?? "none")")
            // What the person's import does to the document: it is edited, and AppKit autosaves it.
            guard let document = NSDocumentController.shared.documents.first else { return }
            // The undo step of the import is what makes AppKit see the document as edited. A window
            // that is not in front may not say so, so the change is declared the same way.
            for _ in 0..<4 where !document.hasUnautosavedChanges { try? await Task.sleep(for: .milliseconds(500)) }
            if !document.hasUnautosavedChanges { document.updateChangeCount(.changeDone) }
            Self.debugLog("import-after-launch edited=\(document.isDocumentEdited) unautosaved=\(document.hasUnautosavedChanges)")
            // The real autosave, which AppKit runs on a background thread.
            document.autosave(withImplicitCancellability: false) { error in
                Self.debugLog("import-after-launch autosaved error=\(error.map { String(describing: $0) } ?? "none")")
            }
            guard let saveTo, !saveTo.isEmpty else { return }
            try? await Task.sleep(for: .seconds(4))
            document.save(to: URL(fileURLWithPath: saveTo), ofType: "com.kyyzen.helix.scene", for: .saveToOperation) { error in
                Self.debugLog("import-after-launch saved error=\(error.map { String(describing: $0) } ?? "none")")
            }
        }
    }

    nonisolated static func debugLog(_ line: String) {
        FileHandle.standardError.write(Data("helix-launch: \(line)\n".utf8)) // l10n-ok: read by launch-smoke.py
    }
}
