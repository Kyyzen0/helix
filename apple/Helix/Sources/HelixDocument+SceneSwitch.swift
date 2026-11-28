import AppKit
import Foundation

/// One scene at a time, without a flat refusal.
///
/// The engine holds one scene, so a second scene cannot be open next to the first. Opening one
/// (File > Open, File > New, a double click in the Finder) used to end in an alert and a window
/// that closed by itself. Now the new window opens and asks: close the scene that is open, and
/// open this one? Close and Open closes the open scene the way the Close command does, so a scene
/// with changes asks to save them first, and the person may cancel there, which leaves both as
/// they were and the new one unopened. A new scene that nothing was done to is replaced without
/// asking, as the untitled window of a document app is (see `EngineHost.claim`).
extension HelixDocument {
    /// Asks the question for this new window. `fileURL` is what it was opened for, nil for a new scene.
    func askToSwitch(fileURL: URL?) {
        guard let other = EngineHost.shared.activeDocument, other !== self else { return }
        let request = SceneSwitchRequest(
            currentName: other.displayTitle,
            newName: SceneNames.name(for: fileURL, untitled: String(localized: "Untitled"))
        )
        switchRequest = request
        // An agent that asked for this scene is waiting for the answer.
        SceneSwitchWatch.shared.questionAsked()
        Self.debugLogSwitch("switch asked current=\(request.currentName) new=\(request.newName)") // l10n-ok: read by launch-smoke.py
    }

    /// Closes the scene that holds the engine, and takes it. Returns false when the person cancelled
    /// the closing (the scene they have is untouched), or the engine could not be taken.
    func confirmSwitch(fileURL: URL?) async -> Bool {
        switchRequest = nil
        if let other = EngineHost.shared.activeDocument, other !== self {
            guard await other.closeForSwitch() else {
                Self.debugLogSwitch("switch kept current") // l10n-ok: read by launch-smoke.py
                SceneSwitchWatch.shared.answered(.kept)
                return false
            }
        }
        do {
            try activate()
        } catch {
            presentedError = error.localizedDescription
            SceneSwitchWatch.shared.answered(.kept)
            return false
        }
        SceneSwitchWatch.shared.answered(.switched)
        Self.debugLogSwitch("switch done") // l10n-ok: read by launch-smoke.py
        await finishOpening(fileURL: fileURL)
        return true
    }

    /// Leaves the scene that is open as it is. The caller closes the new window.
    func cancelSwitch() {
        switchRequest = nil
        SceneSwitchWatch.shared.answered(.kept)
        Self.debugLogSwitch("switch cancelled") // l10n-ok: read by launch-smoke.py
    }

    /// Closes this document's window the way the person would, with the question about unsaved
    /// changes. True when it closed, and the engine is free.
    private func closeForSwitch() async -> Bool {
        guard let system = systemDocument else { return false }
        let closer = SceneCloser()
        sceneCloser = closer
        defer { sceneCloser = nil }
        let closed = await closer.close(system)
        // The window closing gives the engine back; this makes sure of it.
        if closed { relinquish() }
        return closed
    }

    /// With `HELIX_DEBUG_SWITCH=close` or `cancel`, answers the question without a person, which
    /// `Scripts/launch-smoke.py` does to check that a second scene neither crashes nor leaves the
    /// first one half closed. Nil when it is not set.
    static var debugSwitchAnswer: String? {
        ProcessInfo.processInfo.environment["HELIX_DEBUG_SWITCH"]
    }

    /// With `HELIX_DEBUG_OPEN_SECOND=<path>`, opens that file as a second document a little after
    /// the first scene is shown, the way File > Open does.
    func scheduleDebugOpenSecondScene() {
        guard let path = ProcessInfo.processInfo.environment["HELIX_DEBUG_OPEN_SECOND"], !path.isEmpty,
              !Self.openedSecondScene else { return }
        Self.openedSecondScene = true
        Task { @MainActor in
            try? await Task.sleep(for: .seconds(3))
            NSDocumentController.shared.openDocument(withContentsOf: URL(fileURLWithPath: path), display: true) { _, _, error in
                Self.debugLogSwitch("second scene requested error=\(error.map { String(describing: $0) } ?? "none")") // l10n-ok: read by launch-smoke.py
            }
        }
    }

    @MainActor private static var openedSecondScene = false

    private static func debugLogSwitch(_ line: String) {
        guard ProcessInfo.processInfo.environment["HELIX_DEBUG_LAUNCH"] == "1" else { return }
        debugLog(line)
    }
}
