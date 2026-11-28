import XCTest

final class TimelineRulerTests: XCTestCase {
    func testFramesMapToPointsAndBack() {
        let ruler = TimelineRuler(first: 0, last: 100, width: 1024, inset: 12)
        XCTAssertEqual(ruler.pointsPerFrame, 10, accuracy: 1e-9)
        XCTAssertEqual(ruler.x(of: 0), 12, accuracy: 1e-9)
        XCTAssertEqual(ruler.x(of: 100), 1012, accuracy: 1e-9)
        XCTAssertEqual(ruler.frame(atX: ruler.x(of: 37) + 3), 37)
        XCTAssertEqual(ruler.frame(atX: ruler.x(of: 37) + 6), 38)
    }

    func testAPointOutsideTheRulerHoldsToTheEnds() {
        let ruler = TimelineRuler(first: 10, last: 20, width: 500)
        XCTAssertEqual(ruler.frame(atX: -300), 10)
        XCTAssertEqual(ruler.frame(atX: 9000), 20)
        XCTAssertEqual(ruler.frame(atX: .nan), 10)
        XCTAssertEqual(ruler.frame(atX: .infinity), 10)
    }

    func testARulerWithNoRoomOrOneFrameDoesNotDivideByZero() {
        XCTAssertGreaterThan(TimelineRuler(first: 5, last: 5, width: 100).pointsPerFrame, 0)
        XCTAssertGreaterThan(TimelineRuler(first: 1, last: 100, width: 0).pointsPerFrame, 0)
    }

    func testTheTickStepAdaptsToTheWidth() {
        // 240 frames over a wide ruler is labelled every ten or so; a narrow one, coarser.
        let wide = TimelineRuler(first: 1, last: 240, width: 1400)
        let narrow = TimelineRuler(first: 1, last: 240, width: 300)
        XCTAssertLessThan(wide.tickStep(), narrow.tickStep())
        // Always a step a person counts in.
        for width in stride(from: 100.0, through: 4000.0, by: 137) {
            for last in [10, 99, 240, 1000, 90_000] {
                let step = TimelineRuler(first: 1, last: last, width: CGFloat(width)).tickStep()
                let mantissa = step / Int(pow(10, floor(log10(Double(step)))))
                XCTAssertTrue([1, 2, 5].contains(mantissa), "\(step) at \(width) points for \(last) frames")
            }
        }
    }

    func testTheLabelsAreFarEnoughApart() {
        for width in [200.0, 640, 1280, 2560] {
            let ruler = TimelineRuler(first: 1, last: 240, width: CGFloat(width))
            let labels = ruler.labelledFrames()
            XCTAssertEqual(labels.first, 1)
            for (a, b) in zip(labels, labels.dropFirst()) {
                XCTAssertGreaterThanOrEqual(CGFloat(b - a) * ruler.pointsPerFrame, 46 * 0.6 - 0.001)
            }
        }
    }

    func testAFittingRulerLeavesRoomAroundTheRange() {
        let ruler = TimelineRuler.fitting(start: 1, end: 240, width: 1000)
        XCTAssertLessThan(ruler.first, 1)
        XCTAssertGreaterThan(ruler.last, 240)
        XCTAssertLessThan(ruler.x(of: 1), ruler.x(of: 240))
        XCTAssertGreaterThan(ruler.x(of: 1), ruler.inset)
    }

    func testMinorTicksSitBetweenTheLabelsAndNeverOnOne() {
        let ruler = TimelineRuler(first: 1, last: 120, width: 1400)
        let labelled = ruler.tickStep()
        let minor = ruler.minorFrames()
        XCTAssertFalse(minor.isEmpty)
        XCTAssertTrue(minor.allSatisfy { $0 % labelled != 0 })
    }
}

final class TimelineCacheTests: XCTestCase {
    func testConsecutiveFramesMakeOneRun() {
        XCTAssertEqual(TimelineCache.runs(of: [3, 4, 5, 9, 10, 20]), [3...5, 9...10, 20...20])
    }

    func testTheOrderAndTheRepeatsOfTheFramesDoNotMatter() {
        XCTAssertEqual(TimelineCache.runs(of: [10, 4, 3, 5, 4, 9]), [3...5, 9...10])
        XCTAssertEqual(TimelineCache.runs(of: []), [])
        XCTAssertEqual(TimelineCache.runs(of: [-2, -1, 0, 1]), [-2...1])
    }

    func testARunSpansWholeFramesOnTheRuler() {
        let ruler = TimelineRuler(first: 0, last: 100, width: 1024, inset: 12)
        // Ten points a frame: frames 10 to 12 take from the start of frame 10 to the end of 12.
        let span = ruler.span(of: 10...12)
        XCTAssertEqual(span?.x ?? 0, 12 + 9.5 * 10, accuracy: 1e-9)
        XCTAssertEqual(span?.width ?? 0, 30, accuracy: 1e-9)
    }

    func testARunIsHeldToTheRulerAndAnyRunOffItIsLeftOut() {
        let ruler = TimelineRuler(first: 10, last: 20, width: 500, inset: 12)
        let wide = ruler.span(of: 0...100)
        XCTAssertEqual(wide?.x ?? 0, 12, accuracy: 1e-9)
        XCTAssertEqual(wide?.width ?? 0, 500 - 24, accuracy: 1e-9)
        XCTAssertNil(ruler.span(of: 30...40))
        XCTAssertNil(ruler.span(of: 0...5))
        // One frame at the edge keeps its half frame on the inside.
        let edge = ruler.span(of: 20...25)
        XCTAssertEqual(edge?.width ?? 0, ruler.pointsPerFrame / 2, accuracy: 1e-9)
    }
}

final class PlaybackClockTests: XCTestCase {
    private func clock(fps: Double = 24, first: Int = 1, last: Int = 10, loop: Bool = true,
                       mode: PlaybackClock.Mode = .realTime) -> PlaybackClock {
        var clock = PlaybackClock()
        clock.fps = fps
        clock.first = first
        clock.last = last
        clock.loop = loop
        clock.mode = mode
        return clock
    }

    func testARealTimeTickMovesByTheTimeThatWentBy() {
        var clock = clock()
        clock.begin(at: 0)
        // A tenth of a second at 24 fps is two frames and four tenths.
        let step = clock.tick(from: 1, now: 0.1)
        XCTAssertEqual(step.frame, 3)
        XCTAssertEqual(step.advanced, 2)
        XCTAssertEqual(clock.remainder, 0.4, accuracy: 1e-9)
    }

    func testTheRemainderMakesTheRateAverageOut() {
        var clock = clock(fps: 24, last: 10_000)
        clock.begin(at: 0)
        var frame = 1
        // Sixty ticks a second, for ten seconds: 24 frames a second does not divide them.
        for tick in 1...600 {
            frame = clock.tick(from: frame, now: Double(tick) / 60).frame
        }
        XCTAssertEqual(frame, 1 + 240)
    }

    func testASlowCookSkipsFramesInsteadOfFallingBehind() {
        var clock = clock(fps: 24, last: 1000)
        clock.begin(at: 0)
        // A cook that took a quarter of a second: six frames went by.
        let step = clock.tick(from: 1, now: 0.25)
        XCTAssertEqual(step.advanced, 6)
        XCTAssertEqual(step.frame, 7)
    }

    func testEveryFrameModeNeverSkips() {
        var clock = clock(fps: 24, last: 1000, mode: .everyFrame)
        clock.begin(at: 0)
        // A cook that took a quarter of a second still moves one frame, and the backlog is dropped.
        var step = clock.tick(from: 1, now: 0.25)
        XCTAssertEqual(step.frame, 2)
        step = clock.tick(from: 2, now: 0.26)
        XCTAssertEqual(step.advanced, 0, "a frame is not due yet")
        step = clock.tick(from: 2, now: 0.30)
        XCTAssertEqual(step.frame, 3)
    }

    func testLoopingWrapsToTheStart() {
        var clock = clock(first: 1, last: 10, loop: true)
        clock.begin(at: 0)
        let step = clock.tick(from: 9, now: 3.0 / 24)
        // 9 + 3 = 12, which is frame 2 of a range of ten.
        XCTAssertEqual(step.frame, 2)
        XCTAssertFalse(step.stopped)
    }

    func testWithoutLoopingItStopsAtTheEnd() {
        var clock = clock(first: 1, last: 10, loop: false)
        clock.begin(at: 0)
        let step = clock.tick(from: 9, now: 3.0 / 24)
        XCTAssertEqual(step.frame, 10)
        XCTAssertTrue(step.stopped)
    }

    func testARangeOfOneFrameStops() {
        var clock = clock(first: 5, last: 5)
        clock.begin(at: 0)
        XCTAssertTrue(clock.tick(from: 5, now: 0.5).stopped)
    }

    func testAStallDoesNotRunThroughTheRange() {
        var clock = clock(fps: 24, last: 100_000)
        clock.begin(at: 0)
        // An hour of suspension is caught up on for a second at most.
        let step = clock.tick(from: 1, now: 3600)
        XCTAssertEqual(step.advanced, 24)
    }

    func testNothingMovesWithoutARateOrTime() {
        var clock = clock(fps: 0)
        clock.begin(at: 0)
        XCTAssertEqual(clock.tick(from: 3, now: 1).frame, 3)
        var going = self.clock()
        going.begin(at: 5)
        XCTAssertEqual(going.tick(from: 3, now: 4).advanced, 0, "time does not go backwards")
    }
}

final class PlaybackMeterTests: XCTestCase {
    func testItCountsTheFramesOfTheLastSecond() {
        var meter = PlaybackMeter()
        meter.begin(at: 0)
        XCTAssertNil(meter.shownPerSecond(now: 0.1), "not enough time to measure")
        for tick in 1...36 { meter.recordShown(at: Double(tick) / 18) }
        // Eighteen frames a second, for two seconds.
        XCTAssertEqual(meter.shownPerSecond(now: 2)!, 18, accuracy: 1)
    }

    func testItForgetsWhatIsOlderThanTheWindow() {
        var meter = PlaybackMeter()
        meter.begin(at: 0)
        for tick in 1...10 { meter.recordShown(at: Double(tick) / 10) }
        XCTAssertEqual(meter.shownPerSecond(now: 5)!, 0, accuracy: 1e-9)
    }
}

final class TimelineKeysTests: XCTestCase {
    private func key(_ parameter: String, _ component: Int, _ frame: Double, _ value: Double = 0,
                     _ interpolation: KeyInterpolation = .smooth) -> TimelineKey {
        TimelineKey(parameter: parameter, label: parameter.capitalized, component: component, componentCount: 3,
                    frame: frame, value: value, interpolation: interpolation)
    }

    func testKeysOfTheSameFrameShareADiamond() {
        let diamonds = TimelineKeys.merge([
            key("translate", 1, 24), key("translate", 0, 1), key("rotate", 2, 24), key("translate", 0, 24),
        ])
        XCTAssertEqual(diamonds.map(\.frame), [1, 24])
        XCTAssertEqual(diamonds[1].keys.map(\.title), ["Rotate Z", "Translate X", "Translate Y"])
    }

    func testTheTooltipListsEachKeyWithItsValue() {
        let diamond = TimelineKeys.merge([key("translate", 0, 12, 4.5, .linear), key("translate", 2, 12, -1, .step)])[0]
        XCTAssertTrue(diamond.tooltip.contains("Translate X: 4.5 (linear)"), diamond.tooltip)
        XCTAssertTrue(diamond.tooltip.contains("Translate Z: -1 (step)"), diamond.tooltip)
    }

    func testAParameterOfOneValueIsNotNamedByAnAxis() {
        var single = key("radius", 0, 1)
        single.componentCount = 1
        XCTAssertEqual(single.title, "Radius")
    }

    func testADiamondCannotLandOnAKeyOfItsOwnTrack() {
        let all = [key("translate", 0, 1), key("translate", 0, 10), key("rotate", 0, 5)]
        let diamond = TimelineKeys.merge(all)[0]
        XCTAssertNotNil(TimelineKeys.moveRefusal(of: diamond, by: 9, among: all))
        // Another track's key at that frame is no obstacle.
        XCTAssertNil(TimelineKeys.moveRefusal(of: diamond, by: 4, among: all))
        XCTAssertNil(TimelineKeys.moveRefusal(of: diamond, by: 0, among: all))
    }

    func testADragMovesByWholeFramesAndStaysInTheRange() {
        XCTAssertEqual(TimelineKeys.delta(dragging: 25, pointsPerFrame: 10, from: 5, first: 1, last: 240), 3)
        XCTAssertEqual(TimelineKeys.delta(dragging: -400, pointsPerFrame: 10, from: 5, first: 1, last: 240), -4)
        XCTAssertEqual(TimelineKeys.delta(dragging: 99_999, pointsPerFrame: 10, from: 200, first: 1, last: 240), 40)
        XCTAssertEqual(TimelineKeys.delta(dragging: .nan, pointsPerFrame: 10, from: 5, first: 1, last: 240), 0)
        XCTAssertEqual(TimelineKeys.delta(dragging: 5, pointsPerFrame: 0, from: 5, first: 1, last: 240), 0)
    }

    func testTheShortcutsOfTheKeyboard() {
        XCTAssertEqual(TimelineCommand.forKey(code: 49, option: false, otherModifiers: false), .playPause)
        XCTAssertEqual(TimelineCommand.forKey(code: 123, option: false, otherModifiers: false), .stepBack)
        XCTAssertEqual(TimelineCommand.forKey(code: 124, option: false, otherModifiers: false), .stepForward)
        XCTAssertEqual(TimelineCommand.forKey(code: 123, option: true, otherModifiers: false), .goToStart)
        XCTAssertEqual(TimelineCommand.forKey(code: 124, option: true, otherModifiers: false), .goToEnd)
        // Command, shift and control with the arrows are other commands.
        XCTAssertNil(TimelineCommand.forKey(code: 123, option: false, otherModifiers: true))
        XCTAssertNil(TimelineCommand.forKey(code: 49, option: true, otherModifiers: false))
        XCTAssertNil(TimelineCommand.forKey(code: 0, option: false, otherModifiers: false))
    }
}

@MainActor
final class FakeTimeline: TimelineEditing {
    var time = TimelineTime(frame: 1, start: 1, end: 10, fps: 24)
    var keys: [TimelineKey] = []
    var moves: [Double] = []
    var calls: [String] = []
    var refuse: String?
    var cachedFrames: [Int] = []
    var cacheReads = 0

    func readTime() -> TimelineTime { time }
    func loadKeys(node: UInt64?) -> [TimelineKey] { node == nil ? [] : keys }
    func loadCachedFrames() -> [Int] { cacheReads += 1; return cachedFrames }
    func moveToFrame(_ frame: Double) { moves.append(frame); time.frame = frame }
    func setRange(start: Int, end: Int) -> EditOutcome {
        calls.append("range \(start) \(end)")
        if let refuse { return .failed(refuse) }
        time.start = start
        time.end = end
        time.frame = min(max(time.frame, Double(start)), Double(end))
        return .done
    }
    func setFps(_ fps: Double) -> EditOutcome { calls.append("fps \(fps)"); time.fps = fps; return .done }
    func moveKeys(_ keys: [TimelineKey], by delta: Double) -> EditOutcome { calls.append("move \(keys.count) \(delta)"); return .done }
    func setInterpolation(of keys: [TimelineKey], to interpolation: KeyInterpolation) -> EditOutcome {
        calls.append("interpolation \(interpolation.rawValue)")
        return .done
    }
    func removeKeys(_ keys: [TimelineKey]) -> EditOutcome { calls.append("remove \(keys.count)"); return .done }
    func playbackChanged(isPlaying: Bool) { calls.append(isPlaying ? "playing" : "stopped") }
}

@MainActor
final class TimelineModelTests: XCTestCase {
    private var editing = FakeTimeline()
    private var model = TimelineModel()
    private var clock = 0.0
    private var loops = true

    override func setUp() async throws {
        editing = FakeTimeline()
        model = TimelineModel()
        clock = 0
        loops = true
        model.editing = editing
        model.schedulesTimer = false
        model.now = { [unowned self] in self.clock }
        model.loops = { [unowned self] in self.loops }
        model.playsEveryFrame = { false }
        model.reload()
    }

    func testPlayingAdvancesTheFrameWithTheClockAndShowsIt() {
        model.play()
        XCTAssertTrue(model.isPlaying)
        clock = 3.0 / 24
        model.tick()
        XCTAssertEqual(model.time.frame, 4)
        XCTAssertEqual(editing.moves, [4])
    }

    func testAScrubStopsPlaybackAndMovesTheFrame() {
        model.play()
        model.scrub(to: 7)
        XCTAssertFalse(model.isPlaying)
        XCTAssertEqual(model.time.frame, 7)
        XCTAssertTrue(editing.calls.contains("stopped"))
    }

    func testTheFrameIsHeldToTheRange() {
        model.scrub(to: 500)
        XCTAssertEqual(model.time.frame, 10)
        model.scrub(to: -4)
        XCTAssertEqual(model.time.frame, 1)
    }

    func testStepsAndEnds() {
        model.perform(.stepForward)
        XCTAssertEqual(model.time.frame, 2)
        model.perform(.stepBack)
        model.perform(.stepBack)
        XCTAssertEqual(model.time.frame, 1, "it does not step before the start")
        model.perform(.goToEnd)
        XCTAssertEqual(model.time.frame, 10)
        model.perform(.goToStart)
        XCTAssertEqual(model.time.frame, 1)
    }

    func testWithoutLoopingPlaybackStopsAtTheEndAndStartsOverFromThere() {
        loops = false
        editing.time.frame = 9
        model.reload()
        model.play()
        clock = 5.0 / 24
        model.tick()
        XCTAssertEqual(model.time.frame, 10)
        XCTAssertFalse(model.isPlaying)
        // Playing from the end starts over.
        model.play()
        XCTAssertEqual(model.time.frame, 1)
        XCTAssertTrue(model.isPlaying)
    }

    func testLoopingWrapsAround() {
        editing.time.frame = 9
        model.reload()
        model.play()
        clock = 3.0 / 24
        model.tick()
        XCTAssertEqual(model.time.frame, 2)
        XCTAssertTrue(model.isPlaying)
    }

    func testTheKeyboardDrivesIt() {
        XCTAssertTrue(model.handleKey(code: 49, option: false, otherModifiers: false))
        XCTAssertTrue(model.isPlaying)
        XCTAssertTrue(model.handleKey(code: 49, option: false, otherModifiers: false))
        XCTAssertFalse(model.isPlaying)
        XCTAssertFalse(model.handleKey(code: 36, option: false, otherModifiers: false), "return is not the timeline's")
    }

    func testTheMeterReportsTheFramesReallyShown() {
        model.play()
        // The scene runs at 24 and the cook only manages about 12 a second.
        for step in 1...40 {
            clock = Double(step) / 12
            model.tick()
        }
        XCTAssertNotNil(model.meterText)
        XCTAssertTrue(model.meterText?.contains("24 fps") == true, model.meterText ?? "")
        XCTAssertTrue(model.meterText?.contains("12 shown") == true, model.meterText ?? "")
        model.pause()
        XCTAssertNil(model.meterText)
    }

    func testARangeEditStopsPlaybackAndTakesTheNewRange() {
        model.play()
        model.commitEnd("20")
        XCTAssertFalse(model.isPlaying)
        XCTAssertEqual(editing.calls.last, "range 1 20")
        XCTAssertEqual(model.time.end, 20)
        model.commitStart("5")
        XCTAssertEqual(editing.calls.last, "range 5 20")
    }

    func testARefusedRangeSaysWhyAndKeepsTheOldOne() {
        editing.refuse = "The range ends after it starts."
        model.commitStart("50")
        XCTAssertEqual(model.message, "The range ends after it starts.")
        XCTAssertEqual(model.time.start, 1)
    }

    func testTypingGarbageIsNotSentToTheEngine() {
        model.commitEnd("abc")
        model.commitFps("")
        model.commitFrame("x")
        XCTAssertTrue(editing.calls.isEmpty)
        XCTAssertNotNil(model.message)
    }

    func testAnUnchangedRangeIsNotAnEdit() {
        model.commitStart("1")
        model.commitEnd("10")
        XCTAssertTrue(editing.calls.isEmpty)
    }

    func testTheRateIsSentWhenItChanges() {
        model.commitFps("30")
        XCTAssertEqual(editing.calls.last, "fps 30.0")
        XCTAssertEqual(model.time.fps, 30)
    }

    func testTheKeysAreOfTheNodeInFront() {
        editing.keys = [
            TimelineKey(parameter: "t", label: "T", component: 0, componentCount: 1, frame: 3, value: 1, interpolation: .linear),
        ]
        model.setKeyNode(nil)
        XCTAssertTrue(model.diamonds.isEmpty)
        model.setKeyNode(4)
        XCTAssertEqual(model.diamonds.count, 1)
        XCTAssertEqual(model.keyNode, 4)
    }

    func testADiamondThatCannotMoveIsRefusedBeforeTheEngineIsAsked() {
        editing.keys = [
            TimelineKey(parameter: "t", label: "T", component: 0, componentCount: 1, frame: 3, value: 1, interpolation: .linear),
            TimelineKey(parameter: "t", label: "T", component: 0, componentCount: 1, frame: 6, value: 2, interpolation: .linear),
        ]
        model.setKeyNode(4)
        model.move(model.diamonds[0], by: 3)
        XCTAssertTrue(editing.calls.isEmpty)
        XCTAssertNotNil(model.message)
        model.move(model.diamonds[0], by: 1)
        XCTAssertEqual(editing.calls, ["move 1 1.0"])
    }

    func testTheBarOfCachedFramesFollowsTheEngine() {
        XCTAssertEqual(model.cachedRuns, [])
        editing.cachedFrames = [2, 3, 4, 8]
        model.reload()
        XCTAssertEqual(model.cachedRuns, [2...4, 8...8])
        // A cache that was cleared, or an edit that dropped what it held, empties the bar.
        editing.cachedFrames = []
        model.reload()
        XCTAssertEqual(model.cachedRuns, [])
    }

    func testAScrubReadsTheCachedFramesOnce() {
        let before = editing.cacheReads
        editing.cachedFrames = [5]
        model.scrub(to: 5)
        XCTAssertEqual(editing.cacheReads, before + 1)
        XCTAssertEqual(model.cachedRuns, [5...5])
    }

    func testPlaybackReadsTheCachedFramesEveryFewFramesAndWhenItStops() {
        editing.time.end = 100
        model.reload()
        model.play()
        let afterPlay = editing.cacheReads

        // A few frames do not make a lookup.
        clock = 3.0 / 24
        model.tick()
        XCTAssertEqual(editing.cacheReads, afterPlay)

        // Enough of them do.
        editing.cachedFrames = [1, 2, 3, 4]
        clock = Double(TimelineModel.cacheReadInterval + 3) / 24
        model.tick()
        XCTAssertEqual(editing.cacheReads, afterPlay + 1)
        XCTAssertEqual(model.cachedRuns, [1...4])

        // Pausing catches the bar up with the frames that were cooked since.
        editing.cachedFrames = [1, 2, 3, 4, 5, 6]
        model.pause()
        XCTAssertEqual(editing.cacheReads, afterPlay + 2)
        XCTAssertEqual(model.cachedRuns, [1...6])
    }

    func testAKeyEditStopsPlayback() {
        editing.keys = [
            TimelineKey(parameter: "t", label: "T", component: 0, componentCount: 1, frame: 3, value: 1, interpolation: .linear),
        ]
        model.setKeyNode(4)
        model.play()
        model.setInterpolation(of: model.diamonds[0], to: .step)
        XCTAssertFalse(model.isPlaying)
        XCTAssertEqual(editing.calls.last, "interpolation step")
        model.remove(model.diamonds[0])
        XCTAssertEqual(editing.calls.last, "remove 1")
    }
}

/// When the bar is shown: only a scene that plays anything gets one, unless the person decided.
final class TimelineModeTests: XCTestCase {
    private func freshDefaults() -> UserDefaults {
        let defaults = UserDefaults(suiteName: "TimelineModeTests")!
        defaults.removePersistentDomain(forName: "TimelineModeTests")
        return defaults
    }

    func testAutomaticFollowsTheSceneAndTheOthersDoNot() {
        XCTAssertFalse(TimelineMode.automatic.isShown(sceneAnimates: false))
        XCTAssertTrue(TimelineMode.automatic.isShown(sceneAnimates: true))
        XCTAssertTrue(TimelineMode.always.isShown(sceneAnimates: false))
        XCTAssertFalse(TimelineMode.never.isShown(sceneAnimates: true))
    }

    func testAutomaticIsTheDefault() {
        XCTAssertEqual(TimelineSetting.mode(in: freshDefaults()), .automatic)
    }

    func testTheOldOnSettingBecomesAutomaticAndTheOldOffSettingStaysOff() {
        let on = freshDefaults()
        on.set(true, forKey: TimelineSetting.legacyShownKey)
        TimelineSetting.migrateLegacy(in: on)
        XCTAssertEqual(TimelineSetting.mode(in: on), .automatic)
        XCTAssertNil(on.object(forKey: TimelineSetting.legacyShownKey))

        let off = freshDefaults()
        off.set(false, forKey: TimelineSetting.legacyShownKey)
        TimelineSetting.migrateLegacy(in: off)
        XCTAssertEqual(TimelineSetting.mode(in: off), .never)
        XCTAssertEqual(off.string(forKey: TimelineSetting.modeKey), "never")
    }

    func testAModeAlreadyChosenIsNotOverwrittenByTheOldSetting() {
        let defaults = freshDefaults()
        defaults.set(TimelineMode.always.rawValue, forKey: TimelineSetting.modeKey)
        defaults.set(false, forKey: TimelineSetting.legacyShownKey)
        TimelineSetting.migrateLegacy(in: defaults)
        XCTAssertEqual(TimelineSetting.mode(in: defaults), .always)
    }

    func testAnArgumentGivenAsTextIsReadAsAFlag() {
        XCTAssertEqual(TimelineMode.migrated(fromLegacy: "NO"), .never)
        XCTAssertEqual(TimelineMode.migrated(fromLegacy: "YES"), .automatic)
        XCTAssertNil(TimelineMode.migrated(fromLegacy: "maybe"))
        XCTAssertNil(TimelineMode.migrated(fromLegacy: nil))
    }
}
