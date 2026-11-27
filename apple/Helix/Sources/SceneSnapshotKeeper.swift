import CoreFoundation
import Foundation

/// Keeps a `SceneSnapshotCache` up to date from the main thread, whatever changed the scene.
///
/// Instead of following every way the scene can change (an edit, an undo, a drag, a keyframe, a
/// frame of playback, a call of the scripting server, an import), it watches the main run loop,
/// which every one of them goes through: when the loop wakes up the cache is marked as possibly
/// behind, and when it is about to sleep again the scene is captured and stored. So nothing that
/// changes the scene can leave the cache behind for longer than the piece of work that changed it.
///
/// Capturing the scene writes its graph as text, which is small, but a run loop that wakes up a
/// hundred times a second (a mouse move, a frame of playback) is not captured a hundred times a
/// second: at most once every `minimumInterval`, with a timer for the capture a busy loop owes.
@MainActor
final class SceneSnapshotKeeper {
    static let minimumInterval: TimeInterval = 0.2

    private let cache: SceneSnapshotCache
    private let capture: @MainActor () -> Data?
    private var observer: CFRunLoopObserver?
    private var timer: Timer?
    private var lastStore = Date.distantPast
    /// Whether this wake-up of the loop already stored the scene, so that it need not again.
    private var storedThisWakeUp = false

    init(cache: SceneSnapshotCache, capture: @escaping @MainActor () -> Data?) {
        self.cache = cache
        self.capture = capture
    }

    /// Stores the scene now, and starts watching the run loop.
    func start() {
        guard observer == nil else { return }
        refresh()
        let activities: CFRunLoopActivity = [.afterWaiting, .beforeWaiting]
        let observer = CFRunLoopObserverCreateWithHandler(nil, activities.rawValue, true, 0) { [weak self] _, activity in
            // The main run loop calls its observers on the main thread.
            MainActor.assumeIsolated { self?.handle(CFRunLoopActivity(rawValue: activity)) }
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

    private func handle(_ activity: CFRunLoopActivity) {
        if activity == .afterWaiting {
            storedThisWakeUp = false
            cache.markStale()
        } else if activity == .beforeWaiting, !storedThisWakeUp {
            let wait = Self.minimumInterval - Date().timeIntervalSince(lastStore)
            if wait <= 0 {
                refresh()
            } else if timer == nil {
                timer = Timer.scheduledTimer(withTimeInterval: wait, repeats: false) { [weak self] _ in
                    MainActor.assumeIsolated { self?.timerFired() }
                }
            }
        }
    }

    private func timerFired() {
        timer = nil
        refresh()
    }

    private func refresh() {
        guard let data = capture() else { return }
        cache.store(data)
        lastStore = Date()
        storedThisWakeUp = true
    }
}
