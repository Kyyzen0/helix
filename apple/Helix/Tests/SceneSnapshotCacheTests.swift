import XCTest

final class SceneSnapshotCacheTests: XCTestCase {
    private func data(_ text: String) -> Data { Data(text.utf8) }

    func testNothingIsReadBeforeTheSceneIsStored() {
        XCTAssertNil(SceneSnapshotCache().read(timeout: 0.1))
    }

    func testTheLastStoredSceneIsRead() {
        let cache = SceneSnapshotCache()
        cache.store(data("one"))
        cache.store(data("two"))
        XCTAssertEqual(cache.read(timeout: 0.1), data("two"))
    }

    func testAReadOffTheMainThreadSeesWhatTheMainThreadStored() {
        let cache = SceneSnapshotCache()
        cache.store(data("scene"))
        let done = expectation(description: "read")
        DispatchQueue.global().async {
            XCTAssertFalse(Thread.isMainThread)
            XCTAssertEqual(cache.read(timeout: 1), self.data("scene"))
            done.fulfill()
        }
        wait(for: [done], timeout: 5)
    }

    func testAReaderWaitsForTheSceneThatMayBeBehind() {
        let cache = SceneSnapshotCache()
        cache.store(data("old"))
        cache.markStale()
        let done = expectation(description: "read")
        DispatchQueue.global().async {
            XCTAssertEqual(cache.read(timeout: 5), self.data("new"))
            done.fulfill()
        }
        // The main thread finishes what it was doing, and stores the scene as it is.
        Thread.sleep(forTimeInterval: 0.2)
        cache.store(data("new"))
        wait(for: [done], timeout: 5)
    }

    func testAReaderDoesNotWaitForeverForABlockedMainThread() {
        let cache = SceneSnapshotCache()
        cache.store(data("last idle"))
        cache.markStale()
        let started = Date()
        // Nobody stores: the main thread is waiting for this very read.
        XCTAssertEqual(cache.read(timeout: 0.3), data("last idle"))
        XCTAssertGreaterThanOrEqual(Date().timeIntervalSince(started), 0.25)
        XCTAssertLessThan(Date().timeIntervalSince(started), 3)
    }

    func testAStaleMarkBeforeAnyStoreChangesNothing() {
        let cache = SceneSnapshotCache()
        cache.markStale()
        XCTAssertNil(cache.read(timeout: 0.1))
    }

    func testAClosedCacheReadsNothingAndReleasesTheWaitingReader() {
        let cache = SceneSnapshotCache()
        cache.store(data("scene"))
        cache.markStale()
        let done = expectation(description: "read")
        DispatchQueue.global().async {
            XCTAssertNil(cache.read(timeout: 5))
            done.fulfill()
        }
        Thread.sleep(forTimeInterval: 0.2)
        cache.close()
        wait(for: [done], timeout: 5)
        // A late store of a closed document is ignored.
        cache.store(data("late"))
        XCTAssertNil(cache.read(timeout: 0.1))
    }

    func testAReopenedCacheTakesTheSceneAgain() {
        let cache = SceneSnapshotCache()
        cache.close()
        cache.reopen()
        cache.store(data("again"))
        XCTAssertEqual(cache.read(timeout: 0.1), data("again"))
    }
}
