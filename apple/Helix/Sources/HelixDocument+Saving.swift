import CxxStdlib
import Foundation
import HelixCore
import UniformTypeIdentifiers

/// Saving.
///
/// SwiftUI asks `snapshot` from a background thread (the autosave, and the write of an explicit
/// save), where main-actor code must not run: the engine is driven from the main thread only,
/// and Swift 6 stops the process when main-actor code is entered from another thread. A save
/// cannot hop to the main thread and wait either, since the main thread can be waiting for the
/// save (closing the window, quitting). So the main thread keeps the text of the scene in
/// `savedScene` (see `SceneSnapshotKeeper`) and a save off the main thread reads it.
extension HelixDocument {
    /// Starts keeping `savedScene` up to date. Once the scene is loaded.
    func startKeepingSavedScene() {
        savedScene.reopen()
        let keeper = SceneSnapshotKeeper(cache: savedScene) { [weak self] in
            self?.captureSceneForSaving()
        }
        snapshotKeeper = keeper
        keeper.start()
    }

    /// Stops, once the window is closed: there is nothing left to save.
    func stopKeepingSavedScene() {
        snapshotKeeper?.stop()
        snapshotKeeper = nil
        savedScene.close()
    }

    /// The scene as a file holds it, read from the engine. Main thread only.
    private func captureSceneForSaving() -> Data? {
        guard isActive else { return nil }
        let capture = helix.core.captureScene()
        guard capture.status.ok else { return nil }
        return Data(String(engineBytes: capture.data).utf8)
    }

    nonisolated func snapshot(contentType: UTType) throws -> Data {
        if Thread.isMainThread {
            return try MainActor.assumeIsolated {
                guard isActive, let data = captureSceneForSaving() else { throw HelixError.sceneClosed }
                savedScene.store(data)
                return data
            }
        }
        // Off the main thread: what the main thread last kept. It waits a moment when that may be
        // behind, and does not wait for the main thread longer than that (see `SceneSnapshotCache`).
        guard let data = savedScene.read(timeout: Self.saveWaitSeconds) else { throw HelixError.sceneClosed }
        if ProcessInfo.processInfo.environment["HELIX_DEBUG_LAUNCH"] == "1" {
            Self.debugLog("snapshot off the main thread bytes=\(data.count)")
        }
        return data
    }

    nonisolated static var saveWaitSeconds: TimeInterval { 3 }

    nonisolated func fileWrapper(snapshot: Data, configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: snapshot)
    }
}
