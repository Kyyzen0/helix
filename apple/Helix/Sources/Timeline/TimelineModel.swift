import Combine
import Foundation

/// The time of the scene, as the timeline shows it.
struct TimelineTime: Equatable, Sendable {
    var frame: Double = 1
    var start: Int = 1
    var end: Int = 240
    var fps: Double = 24
}

/// The settings of the timeline kept in the user defaults, so the menu, the bar and every window
/// agree and a choice survives a restart.
enum TimelineSetting {
    static let shownKey = "timelineShown"
    static let loopKey = "timelineLoop"
    static let everyFrameKey = "timelinePlayEveryFrame"
    static let lowerPaneKey = "lowerPane"

    private static func flag(_ key: String, default value: Bool) -> Bool {
        let defaults = UserDefaults.standard
        // bool(forKey:) also reads the YES and NO of a launch argument, which arrives as text.
        return defaults.object(forKey: key) == nil ? value : defaults.bool(forKey: key)
    }

    /// Whether playback goes back to the start at the end of the range. On until it is turned off.
    static var loops: Bool { flag(loopKey, default: true) }

    /// Whether playback shows every frame, instead of keeping up with the clock.
    static var playsEveryFrame: Bool { flag(everyFrameKey, default: false) }

    /// Which of the two views the bottom pane of a window shows.
    enum LowerPane: String {
        case network, spreadsheet
    }

    static var lowerPane: LowerPane {
        UserDefaults.standard.string(forKey: lowerPaneKey).flatMap(LowerPane.init(rawValue:)) ?? .network
    }
}

/// What the timeline asks of the document. The calls that change the scene (range, rate, keys)
/// are each one step of undo. Moving the playhead is not an edit: it is a view of the scene, so it
/// never reaches the undo manager, and an undo leaves the playhead where it is.
@MainActor
protocol TimelineEditing: AnyObject {
    func readTime() -> TimelineTime
    /// The keys of a node, or of nothing when the node is nil.
    func loadKeys(node: UInt64?) -> [TimelineKey]
    /// The whole frames the cook cache holds a cook of the displayed node for, which the bar under
    /// the ruler shows. Reading it never cooks.
    func loadCachedFrames() -> [Int]
    /// Moves the playhead. The viewport, the inspector and the spreadsheet follow it.
    func moveToFrame(_ frame: Double)
    func setRange(start: Int, end: Int) -> EditOutcome
    func setFps(_ fps: Double) -> EditOutcome
    func moveKeys(_ keys: [TimelineKey], by delta: Double) -> EditOutcome
    func setInterpolation(of keys: [TimelineKey], to interpolation: KeyInterpolation) -> EditOutcome
    func removeKeys(_ keys: [TimelineKey]) -> EditOutcome
    /// Playback started or stopped, so the rest of the window can leave out what costs too much
    /// to redraw at every frame and catch up when it ends.
    func playbackChanged(isPlaying: Bool)
}

/// What the timeline of a window shows and does: the playhead, the range, playback, and the keys of
/// the node in front. It holds no engine: it reads and writes through `editing`, so the rules of
/// playback and of the ruler can be tested without one.
@MainActor
final class TimelineModel: ObservableObject {
    @Published private(set) var time = TimelineTime()
    @Published private(set) var isPlaying = false
    @Published private(set) var diamonds: [TimelineDiamond] = []
    /// The frames the cook cache holds a cook of the displayed node for, as runs of frames. They
    /// are the thin bar under the ticks: a frame in it is shown without a cook.
    @Published private(set) var cachedRuns: [ClosedRange<Int>] = []
    /// How playback is going, such as "24 fps · 18 shown". Nil when it is not playing.
    @Published private(set) var meterText: String?
    /// A refusal of the engine, to show beside the fields until the next edit.
    @Published private(set) var message: String?

    weak var editing: TimelineEditing?
    /// The node whose keys are on the ruler: the one selected, or the one displayed.
    private(set) var keyNode: UInt64?

    /// The time in seconds, on a clock that only goes forward. Replaced in tests.
    var now: () -> Double = { ProcessInfo.processInfo.systemUptime }
    /// Whether playback loops, and whether it shows every frame. Replaced in tests.
    var loops: () -> Bool = { TimelineSetting.loops }
    var playsEveryFrame: () -> Bool = { TimelineSetting.playsEveryFrame }
    /// Whether a timer drives the playback. Tests turn it off and call `tick()` themselves.
    var schedulesTimer = true

    private var clock = PlaybackClock()
    private var meter = PlaybackMeter()
    private var timer: Timer?
    private var lastMeterUpdate = 0.0
    // The frames shown since the bar of cached frames was read, which playback reads again every
    // few of.
    private var framesSinceCacheRead = 0
    /// How many frames playback shows between two readings of the cached frames.
    static let cacheReadInterval = 12

    nonisolated init() {}

    // MARK: Reading

    /// Reads the time and the keys again, after an edit, an undo or a new scene.
    func reload() {
        guard let editing else { return }
        time = editing.readTime()
        diamonds = TimelineKeys.merge(editing.loadKeys(node: keyNode))
        refreshCache()
    }

    func setKeyNode(_ node: UInt64?) {
        keyNode = node
        guard let editing else { return }
        diamonds = TimelineKeys.merge(editing.loadKeys(node: node))
        refreshCache()
    }

    /// Reads the cached frames again, which a cook, an edit or a new display node changes.
    func refreshCache() {
        guard let editing else { return }
        framesSinceCacheRead = 0
        let runs = TimelineCache.runs(of: editing.loadCachedFrames())
        if runs != cachedRuns { cachedRuns = runs }
    }

    /// Reads only the time, which is all a frame of playback changes.
    func reloadTime() {
        guard let editing else { return }
        time = editing.readTime()
    }

    // MARK: The playhead

    /// Moves the playhead to a frame, held to the range. Stops the playback, since a person who
    /// moves it by hand is no longer watching it go.
    func scrub(to frame: Int) {
        pause()
        goTo(Double(frame))
        refreshCache()
    }

    private func goTo(_ frame: Double) {
        let held = min(max(frame, Double(time.start)), Double(time.end))
        editing?.moveToFrame(held)
        time.frame = editing?.readTime().frame ?? held
    }

    func perform(_ command: TimelineCommand) {
        switch command {
        case .playPause: isPlaying ? pause() : play()
        case .stepBack: scrub(to: Int(time.frame.rounded(.up)) - 1)
        case .stepForward: scrub(to: Int(time.frame.rounded(.down)) + 1)
        case .goToStart: scrub(to: time.start)
        case .goToEnd: scrub(to: time.end)
        }
    }

    /// Handles a key pressed while a view of the window that takes the keys has them. Returns
    /// whether the timeline took it.
    func handleKey(code: UInt16, option: Bool, otherModifiers: Bool) -> Bool {
        guard let command = TimelineCommand.forKey(code: code, option: option, otherModifiers: otherModifiers) else {
            return false
        }
        perform(command)
        return true
    }

    // MARK: Playback

    func play() {
        guard !isPlaying, editing != nil else { return }
        reloadTime()
        // Playing from the end of a range that does not loop starts it over, as a player does.
        if !loops(), time.frame >= Double(time.end) { goTo(Double(time.start)) }
        isPlaying = true
        message = nil
        configureClock()
        let start = now()
        clock.begin(at: start)
        meter.begin(at: start)
        lastMeterUpdate = start
        meterText = nil
        editing?.playbackChanged(isPlaying: true)
        if schedulesTimer { startTimer() }
    }

    func pause() {
        guard isPlaying else { return }
        timer?.invalidate()
        timer = nil
        isPlaying = false
        meterText = nil
        meter.reset()
        editing?.playbackChanged(isPlaying: false)
        refreshCache()
    }

    private func configureClock() {
        clock.fps = time.fps
        clock.first = time.start
        clock.last = time.end
        clock.loop = loops()
        clock.mode = playsEveryFrame() ? .everyFrame : .realTime
    }

    private func startTimer() {
        // Fast enough to catch a frame of a 60 or 120 fps scene on time. The clock decides what
        // a tick is worth, so a timer that fires early or late changes nothing but the smoothness.
        let timer = Timer(timeInterval: 1.0 / 120.0, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.tick() }
        }
        RunLoop.main.add(timer, forMode: .common)
        self.timer = timer
    }

    /// One beat of the playback: moves the playhead for the time that went by, and shows the frame.
    func tick() {
        guard isPlaying, editing != nil else { return }
        // The settings are read again, so a change of the menu applies without stopping.
        configureClock()
        let step = clock.tick(from: Int(time.frame.rounded()), now: now())
        if step.advanced > 0 {
            goTo(Double(step.frame))
            meter.recordShown(at: now())
            // Reading the frames at every one would be a lookup at every frame of a cook, so the
            // bar catches up every few frames, and when playback stops.
            framesSinceCacheRead += step.advanced
            if framesSinceCacheRead >= Self.cacheReadInterval { refreshCache() }
        }
        if step.stopped {
            pause()
            return
        }
        updateMeter()
    }

    private func updateMeter() {
        let current = now()
        // Four times a second, so the text is readable and not a blur of digits.
        guard current - lastMeterUpdate >= 0.25 else { return }
        lastMeterUpdate = current
        if let shown = meter.shownPerSecond(now: current) {
            meterText = String(localized: "\(NumberText.format(time.fps)) fps · \(Int(shown.rounded())) shown")
        }
    }

    // MARK: Range and rate

    func commitStart(_ text: String) { commitRange(start: parseFrame(text), end: nil) }
    func commitEnd(_ text: String) { commitRange(start: nil, end: parseFrame(text)) }

    private func parseFrame(_ text: String) -> Int? {
        switch NumberText.parseInt(text) {
        case .value(let number): return number
        case .invalid(let reason):
            message = reason
            return nil
        }
    }

    private func commitRange(start: Int?, end: Int?) {
        guard start != nil || end != nil, let editing else { return }
        pause()
        let newStart = start ?? time.start
        let newEnd = end ?? time.end
        guard newStart != time.start || newEnd != time.end else { return }
        finish(editing.setRange(start: newStart, end: newEnd))
    }

    func commitFps(_ text: String) {
        guard let editing else { return }
        switch NumberText.parseFloat(text) {
        case .value(let fps):
            guard fps != time.fps else { return }
            pause()
            finish(editing.setFps(fps))
        case .invalid(let reason):
            message = reason
            reload()
        }
    }

    func commitFrame(_ text: String) {
        switch NumberText.parseFloat(text) {
        case .value(let frame):
            pause()
            goTo(frame)
        case .invalid(let reason):
            message = reason
            reloadTime()
        }
    }

    private func finish(_ outcome: EditOutcome) {
        message = outcome.error
        reload()
    }

    // MARK: Keys

    /// Moves every key of a diamond by a whole number of frames, as one step, or says why not.
    func move(_ diamond: TimelineDiamond, by delta: Double) {
        guard delta != 0, let editing else { return }
        let all = diamonds.flatMap(\.keys)
        if let refusal = TimelineKeys.moveRefusal(of: diamond, by: delta, among: all) {
            message = refusal
            return
        }
        pause()
        finish(editing.moveKeys(diamond.keys, by: delta))
    }

    func setInterpolation(of diamond: TimelineDiamond, to interpolation: KeyInterpolation) {
        guard let editing else { return }
        pause()
        finish(editing.setInterpolation(of: diamond.keys, to: interpolation))
    }

    func remove(_ diamond: TimelineDiamond) {
        guard let editing else { return }
        pause()
        finish(editing.removeKeys(diamond.keys))
    }
}
