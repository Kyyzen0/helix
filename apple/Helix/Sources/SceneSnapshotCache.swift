import Foundation

/// The text of the scene, kept so that a save can be served from any thread.
///
/// SwiftUI asks a document for what to save from a background thread (autosave, and the write of
/// an explicit save: AppKit writes off the main thread). The engine is driven from the main
/// thread only, and the main thread may be waiting for that very save (closing the window,
/// quitting), so the save can neither call the engine nor hop to the main thread and wait. It
/// reads this cache instead, which the main thread keeps up to date (see `SceneSnapshotKeeper`).
///
/// Whether the cache is up to date is exact, not a guess: the engine counts its changes
/// (`sceneRevision`, an atomic any thread can read), and the main thread stores the text with
/// the revision it read before it captured the text. The text is the scene when that revision is
/// still the engine's. A save that finds the text current returns at once, and so cannot wait for
/// a main thread that waits for it. One that finds it behind waits, up to a limit, for the main
/// thread to store a newer capture, and fails (rather than saving old text) when it does not.
final class SceneSnapshotCache: @unchecked Sendable {
    /// What a read comes to.
    enum Outcome: Equatable {
        /// The scene, as of a revision no older than the one the read started with.
        case scene(Data)
        /// The scene is gone (the window closed): there is nothing to save any more.
        case closed
        /// The main thread did not bring the text up to date in time.
        case behind
    }

    private let condition = NSCondition()
    private let currentRevision: @Sendable () -> UInt64
    private var text: Data?
    private var storedRevision: UInt64 = 0
    private var closed = false

    /// `currentRevision` is the engine's `sceneRevision`, which may be called from any thread.
    init(currentRevision: @escaping @Sendable () -> UInt64) {
        self.currentRevision = currentRevision
    }

    /// Records a capture of the scene. `revision` is what the engine said just *before* the
    /// capture was made, so a change after it makes the capture count as behind.
    func store(_ data: Data, revision: UInt64) {
        condition.lock()
        if !closed, text == nil || revision >= storedRevision {
            text = data
            storedRevision = revision
        }
        condition.broadcast()
        condition.unlock()
    }

    /// Whether what is stored is the scene as it is now.
    var isFresh: Bool {
        condition.lock()
        defer { condition.unlock() }
        return text != nil && storedRevision == currentRevision()
    }

    /// The scene is gone (the window closed).
    func close() {
        condition.lock()
        closed = true
        text = nil
        condition.broadcast()
        condition.unlock()
    }

    /// Opens the cache again, for a document that takes the engine back.
    func reopen() {
        condition.lock()
        closed = false
        condition.unlock()
    }

    /// The scene as it was when the read started or later. Returns at once when the stored text
    /// is current; otherwise waits up to `timeout` seconds for a capture that is.
    func read(timeout: TimeInterval) -> Outcome {
        let deadline = Date(timeIntervalSinceNow: timeout)
        condition.lock()
        defer { condition.unlock() }
        let wanted = currentRevision()
        while true {
            if closed { return .closed }
            if let text, storedRevision >= wanted { return .scene(text) }
            if !condition.wait(until: deadline) { return .behind }
        }
    }
}
