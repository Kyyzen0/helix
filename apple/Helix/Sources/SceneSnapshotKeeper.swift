import CoreFoundation
import Foundation

/// Keeps a `SceneSnapshotCache` up to date from the main thread, whatever changed the scene.
///
/// The engine counts its changes (`sceneRevision`), so what has to be done is known exactly: when
/// the main run loop is about to go idle and the stored text is not the scene any more, capture
/// it. Every way the scene can change (an edit, an undo, a drag, a keyframe, a frame of playback,
/// a call of the scripting server, an import) ends in the loop going idle, so nothing leaves the
/// cache behind for longer than the piece of work that changed it. A capture is never made in
/// the middle of a piece of work: and if one were, a change after it moves the revision, so the
/// cache would still read as behind.
///
/// Capturing the scene writes its graph as text, which is small, but a loop that goes idle a
/// hundred times a second (a frame of playback) is not captured a hundred times a second: at
/// most once every `minimumInterval`. When that holds a capture back, a timer only wakes the loop
/// at the time it is due; the capture itself is still made when the loop goes idle.
@MainActor
final class SceneSnapshotKeeper {
    static let minimumInterval: TimeInterval = 0.2

    private let cache: SceneSnapshotCache
    private let revision: () -> UInt64
    private let capture: @MainActor () -> Data?
    private var observer: CFRunLoopObserver?
    private var timer: Timer?
    private var lastStore = Date.distantPast

    init(cache: SceneSnapshotCache, revision: @escaping () -> UInt64, capture: @escaping @MainActor () -> Data?) {
        self.cache = cache
        self.revision = revision
        self.capture = capture
    }

    /// Stores the scene now, and starts watching the run loop.
    func start() {
        guard observer == nil else { return }
        refresh()
        // Common modes: a sheet, a panel or the review of unsaved documents at quit runs the loop
        // in a mode of its own, and a save can be asked for there.
        let observer = CFRunLoopObserverCreateWithHandler(
            nil, CFRunLoopActivity.beforeWaiting.rawValue, true, 0
        ) { [weak self] _, _ in
            // The main run loop calls its observers on the main thread.
            MainActor.assumeIsolated { self?.loopIsGoingIdle() }
        }
        CFRunLoopAddObserver(CFRunLoopGetMain(), observer, .commonModes)
        self.observer = observer
    }

    func stop() {
        if let observer { CFRunLoopRemoveObserver(CFRunLoopGetMain(), observer, .commonModes) }
        observer = nil
        timer?.invalidate()
        timer = nil
    }

    private func loopIsGoingIdle() {
        guard !cache.isFresh else { return }
        let wait = Self.minimumInterval - Date().timeIntervalSince(lastStore)
        if wait <= 0 {
            refresh()
        } else if timer == nil {
            // Only to wake the loop when the capture is due: the loop goes idle again after it.
            timer = Timer.scheduledTimer(withTimeInterval: wait, repeats: false) { [weak self] _ in
                MainActor.assumeIsolated { self?.timer = nil }
            }
        }
    }

    private func refresh() {
        // The revision first: a change after it, even in the capture's own call, reads as behind.
        let seen = revision()
        guard let data = capture() else { return }
        cache.store(data, revision: seen)
        lastStore = Date()
    }
}
