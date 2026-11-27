import XCTest

final class SceneSnapshotCacheTests: XCTestCase {
    /// The engine's revision, which a test moves by hand.
    private final class Revision: @unchecked Sendable {
        private let lock = NSLock()
        private var value: UInt64 = 1
        var current: UInt64 { lock.lock(); defer { lock.unlock() }; return value }
        func bump() { lock.lock(); value += 1; lock.unlock() }
    }

    private let revision = Revision()
    private lazy var cache = SceneSnapshotCache(currentRevision: { [revision] in revision.current })

    private func data(_ text: String) -> Data { Data(text.utf8) }

    func testNothingIsReadBeforeTheSceneIsStored() {
        XCTAssertEqual(cache.read(timeout: 0.1), .behind)
        XCTAssertFalse(cache.isFresh)
    }

    func testACurrentCaptureIsReadAtOnce() {
        cache.store(data("scene"), revision: revision.current)
        XCTAssertTrue(cache.isFresh)
        let started = Date()
        XCTAssertEqual(cache.read(timeout: 5), .scene(data("scene")))
        XCTAssertLessThan(Date().timeIntervalSince(started), 1)
    }

    func testAReadOffTheMainThreadSeesWhatTheMainThreadStored() {
        cache.store(data("scene"), revision: revision.current)
        let done = expectation(description: "read")
        DispatchQueue.global().async {
            XCTAssertFalse(Thread.isMainThread)
            XCTAssertEqual(self.cache.read(timeout: 1), .scene(self.data("scene")))
            done.fulfill()
        }
        wait(for: [done], timeout: 5)
    }

    func testACaptureMadeBeforeAChangeIsNotFresh() {
        cache.store(data("old"), revision: revision.current)
        revision.bump()
        XCTAssertFalse(cache.isFresh)
    }

    func testACaptureRecordedWithAnOlderRevisionNeverReadsAsCurrent() {
        // The scene changed while it was being captured: the revision read before the capture is
        // the one stored, so the capture is behind.
        let before = revision.current
        revision.bump()
        cache.store(data("half way"), revision: before)
        XCTAssertFalse(cache.isFresh)
        XCTAssertEqual(cache.read(timeout: 0.2), .behind)
    }

    func testAReaderWaitsForACaptureOfTheSceneAsItIs() {
        cache.store(data("old"), revision: revision.current)
        revision.bump()
        let now = revision.current
        let done = expectation(description: "read")
        DispatchQueue.global().async {
            XCTAssertEqual(self.cache.read(timeout: 5), .scene(self.data("new")))
            done.fulfill()
        }
        // The main thread finishes what it was doing, and stores the scene as it is.
        Thread.sleep(forTimeInterval: 0.2)
        cache.store(data("new"), revision: now)
        wait(for: [done], timeout: 5)
    }

    func testAReaderDoesNotWaitForAnEverChangingScene() {
        cache.store(data("old"), revision: revision.current)
        revision.bump()
        let wanted = revision.current
        let done = expectation(description: "read")
        DispatchQueue.global().async {
            // The scene moves on again while the reader waits (playback): the capture of the
            // revision the read started with is enough.
            XCTAssertEqual(self.cache.read(timeout: 5), .scene(self.data("a moment ago")))
            done.fulfill()
        }
        Thread.sleep(forTimeInterval: 0.1)
        revision.bump()
        cache.store(data("a moment ago"), revision: wanted)
        wait(for: [done], timeout: 5)
    }

    func testAReaderGivesUpWhenTheMainThreadDoesNotAnswer() {
        cache.store(data("last idle"), revision: revision.current)
        revision.bump()
        let started = Date()
        // Nobody stores: the main thread is waiting for this very read. The old text is not
        // returned, which would save without the last changes.
        XCTAssertEqual(cache.read(timeout: 0.3), .behind)
        XCTAssertGreaterThanOrEqual(Date().timeIntervalSince(started), 0.25)
        XCTAssertLessThan(Date().timeIntervalSince(started), 3)
    }

    func testAnOlderCaptureDoesNotReplaceANewerOne() {
        let first = revision.current
        revision.bump()
        cache.store(data("newer"), revision: revision.current)
        cache.store(data("older"), revision: first)
        XCTAssertEqual(cache.read(timeout: 0.1), .scene(data("newer")))
    }

    func testAClosedCacheReadsClosedAndReleasesTheWaitingReader() {
        cache.store(data("scene"), revision: revision.current)
        revision.bump()
        let done = expectation(description: "read")
        DispatchQueue.global().async {
            XCTAssertEqual(self.cache.read(timeout: 5), .closed)
            done.fulfill()
        }
        Thread.sleep(forTimeInterval: 0.2)
        cache.close()
        wait(for: [done], timeout: 5)
        // A late store of a closed document is ignored.
        cache.store(data("late"), revision: revision.current)
        XCTAssertEqual(cache.read(timeout: 0.1), .closed)
    }

    func testAReopenedCacheTakesTheSceneAgain() {
        cache.close()
        cache.reopen()
        cache.store(data("again"), revision: revision.current)
        XCTAssertEqual(cache.read(timeout: 0.1), .scene(data("again")))
    }
}
