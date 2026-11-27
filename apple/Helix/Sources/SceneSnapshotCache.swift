import Foundation

/// The text of the scene, kept so that a save can be served from any thread.
///
/// SwiftUI asks a document for what to save from a background thread (autosave, and the write of
/// an explicit save: AppKit writes off the main thread). The engine is driven from the main
/// thread only, and the main thread may be waiting for that very save (closing the window,
/// quitting), so the save can neither call the engine nor hop to the main thread and wait. It
/// reads this cache instead, which the main thread keeps up to date (see `HelixDocument+Saving`).
///
/// The main thread says when the cache may be behind (`markStale`, when it wakes up to handle
/// something) and gives the text when it is done (`store`, when it is idle again). A reader that
/// finds the cache possibly behind waits for `store`, a short while at most, so that what is
/// saved is what the person last saw. If the main thread cannot answer in that time, because it
/// is itself blocked waiting for this save, the reader gets the text of the last idle moment: no
/// deadlock, and nothing older than the work the blocked main thread had just started.
final class SceneSnapshotCache: @unchecked Sendable {
    private let condition = NSCondition()
    private var text: Data?
    private var stale = false
    private var closed = false

    /// Records the scene as it is now. Wakes the readers that wait for it.
    func store(_ data: Data) {
        condition.lock()
        if !closed {
            text = data
            stale = false
        }
        condition.broadcast()
        condition.unlock()
    }

    /// Notes that the scene may have changed since the last `store`.
    func markStale() {
        condition.lock()
        if text != nil { stale = true }
        condition.unlock()
    }

    /// The scene is gone (the window closed): there is nothing to save any more.
    func close() {
        condition.lock()
        closed = true
        text = nil
        stale = false
        condition.broadcast()
        condition.unlock()
    }

    /// Opens the cache again, for a document that takes the engine back.
    func reopen() {
        condition.lock()
        closed = false
        condition.unlock()
    }

    /// The text of the scene, or nil when it is closed or was never stored. Waits up to
    /// `timeout` seconds for the main thread to bring it up to date when it may be behind.
    func read(timeout: TimeInterval) -> Data? {
        let deadline = Date(timeIntervalSinceNow: timeout)
        condition.lock()
        defer { condition.unlock() }
        while stale, !closed, condition.wait(until: deadline) {}
        return text
    }
}
