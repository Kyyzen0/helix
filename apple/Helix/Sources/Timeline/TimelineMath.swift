import CoreGraphics
import Foundation

// The arithmetic of the timeline: where a frame sits on the ruler, how a tick of the clock
// moves the playhead, and how fast the frames really went. None of it touches the engine or a
// view, so every rule can be tested on its own.

/// Maps frames to points on the ruler and back.
struct TimelineRuler: Equatable {
    var first: Int
    var last: Int
    /// The width of the whole ruler, in points.
    var width: CGFloat
    /// The room left on each side, so the first and the last frame are not cut by the edge.
    var inset: CGFloat = 12

    /// The points one frame takes. A range of one frame, or a ruler with no room, is one point
    /// wide a frame so nothing divides by zero.
    var pointsPerFrame: CGFloat {
        let span = CGFloat(max(last - first, 1))
        return max(width - 2 * inset, 1) / span
    }

    /// A ruler that shows a range with a little room on each side, so the first and the last frame
    /// have something beside them and the range reads as a stretch of the ruler.
    static func fitting(start: Int, end: Int, width: CGFloat) -> TimelineRuler {
        let pad = max(2, Int((Double(max(end - start, 1)) * 0.03).rounded(.up)))
        return TimelineRuler(first: start - pad, last: end + pad, width: width)
    }

    func x(of frame: Double) -> CGFloat {
        inset + CGFloat(frame - Double(first)) * pointsPerFrame
    }

    /// The whole frame under a point, held to the range.
    func frame(atX x: CGFloat) -> Int {
        guard x.isFinite else { return first }
        let raw = ((x - inset) / pointsPerFrame).rounded() + CGFloat(first)
        return Int(min(max(raw, CGFloat(first)), CGFloat(last)))
    }

    /// The frames between two labelled ticks, so that the labels are at least `minLabelSpacing`
    /// points apart: 1, 2, 5, 10, 20, 50 and on, which is what a person counts in.
    func tickStep(minLabelSpacing: CGFloat = 46) -> Int {
        var step = 1
        var scale = 1
        let multipliers = [1, 2, 5]
        search: while true {
            for multiplier in multipliers {
                step = multiplier * scale
                if CGFloat(step) * pointsPerFrame >= minLabelSpacing { break search }
            }
            scale *= 10
            if scale > 100_000_000 { break }
        }
        return step
    }

    /// The labelled frames: `origin` (the first frame of the range, by default the first of the
    /// ruler) and then the multiples of the step after it.
    func labelledFrames(from origin: Int? = nil, minLabelSpacing: CGFloat = 46) -> [Int] {
        let step = tickStep(minLabelSpacing: minLabelSpacing)
        let first = origin ?? self.first
        // A range from 1 labels 1 and then the multiples of the step, as a ruler of frames does.
        var frames: [Int] = [first]
        var frame = ((first + step) / step) * step
        if frame <= first { frame += step }
        while frame <= last {
            // A multiple that falls so close to the first label that they would overlap is left out.
            if CGFloat(frame - first) * pointsPerFrame >= minLabelSpacing * 0.6 { frames.append(frame) }
            frame += step
        }
        return frames
    }

    /// The unlabelled ticks between the labels, when the frames are far enough apart to draw them.
    func minorFrames(minSpacing: CGFloat = 5) -> [Int] {
        let labelled = tickStep()
        // Subdivide a labelled step in ten, five or two parts, whichever still leaves the ticks apart.
        for divisions in [10, 5, 2] where labelled % divisions == 0 {
            let minor = labelled / divisions
            if CGFloat(minor) * pointsPerFrame >= minSpacing {
                return stride(from: (first / minor) * minor, through: last, by: minor).filter { $0 >= first && $0 % labelled != 0 }
            }
        }
        return []
    }
}

/// The frames the cook cache holds a cook for, as the bar under the ruler shows them.
enum TimelineCache {
    /// The frames as runs of consecutive frames, in order. A frame given twice counts once.
    static func runs(of frames: [Int]) -> [ClosedRange<Int>] {
        var runs: [ClosedRange<Int>] = []
        for frame in Set(frames).sorted() {
            if let last = runs.last, last.upperBound + 1 == frame {
                runs[runs.count - 1] = last.lowerBound...frame
            } else {
                runs.append(frame...frame)
            }
        }
        return runs
    }
}

extension TimelineRuler {
    /// Where a run of frames sits on the ruler: from the start of its first frame to the end of
    /// its last, since a frame is a stretch of the ruler and not a point on it. Held to the
    /// ruler, and nil for a run entirely off it.
    func span(of run: ClosedRange<Int>) -> (x: CGFloat, width: CGFloat)? {
        let lower = max(run.lowerBound, first)
        let upper = min(run.upperBound, last)
        guard lower <= upper else { return nil }
        let start = x(of: Double(lower) - 0.5)
        let end = x(of: Double(upper) + 0.5)
        return (max(start, inset), max(min(end, width - inset) - max(start, inset), 0))
    }
}

/// How the playhead advances with the clock.
struct PlaybackClock: Equatable {
    enum Mode: String, Equatable, Sendable {
        /// Frames go by with the time: a cook that takes longer than a frame skips the frames it missed.
        case realTime
        /// Every frame is shown, one after the other, at no more than the frame rate.
        case everyFrame
    }

    var mode: Mode = .realTime
    var loop = true
    var fps: Double = 24
    var first = 1
    var last = 240

    /// The time, in frames, that has gone by and has not made a whole frame yet. It is kept, as
    /// the Qt timeline did, so a rate that does not divide the ticks of the clock still averages
    /// out to the right speed.
    private(set) var remainder: Double = 0
    private var lastTick: Double?

    /// What a tick came to.
    struct Step: Equatable {
        /// The frame the playhead sits on after the tick.
        var frame: Int
        /// Whole frames the playhead went forward, counting the ones that were skipped.
        var advanced: Int
        /// Whether the tick made the playhead stop, at the end of a range that does not loop.
        var stopped: Bool
    }

    /// Starts counting time from `now`.
    mutating func begin(at now: Double) {
        lastTick = now
        remainder = 0
    }

    /// Moves the playhead for the time that went by since the last tick.
    mutating func tick(from frame: Int, now: Double) -> Step {
        let previous = lastTick ?? now
        lastTick = now
        guard fps > 0, fps.isFinite else { return Step(frame: frame, advanced: 0, stopped: false) }

        // A stall (the app was suspended, a long cook) must not make the playhead run through the
        // whole range afterwards: at most a second of frames is caught up on.
        let elapsed = min(max(now - previous, 0), 1)
        remainder += elapsed * fps

        var whole = Int(remainder.rounded(.down))
        switch mode {
        case .realTime:
            remainder -= Double(whole)
        case .everyFrame:
            // One frame at most per tick, and the backlog of a slow cook is dropped, so every
            // frame is shown and the playback is simply slower.
            whole = min(whole, 1)
            remainder = whole == 1 ? 0 : remainder
        }
        guard whole > 0 else { return Step(frame: frame, advanced: 0, stopped: false) }
        return move(from: frame, by: whole)
    }

    /// Moves the playhead forward by whole frames, wrapping or stopping at the end of the range.
    func move(from frame: Int, by frames: Int) -> Step {
        let length = last - first + 1
        guard length > 1 else { return Step(frame: first, advanced: frames, stopped: true) }
        let target = frame + frames
        if target <= last { return Step(frame: max(target, first), advanced: frames, stopped: false) }
        if loop {
            return Step(frame: first + (target - first) % length, advanced: frames, stopped: false)
        }
        return Step(frame: last, advanced: frames, stopped: true)
    }
}

/// Counts the frames the playhead really showed, to tell a person how fast the playback goes.
struct PlaybackMeter: Equatable {
    /// The seconds the count looks back over.
    var window: Double = 1

    private var shownAt: [Double] = []
    private var startedAt: Double?

    mutating func reset() {
        shownAt.removeAll()
        startedAt = nil
    }

    mutating func begin(at now: Double) {
        reset()
        startedAt = now
    }

    /// Notes that a frame was drawn at `now`.
    mutating func recordShown(at now: Double) {
        shownAt.append(now)
        trim(now)
    }

    private mutating func trim(_ now: Double) {
        let cutoff = now - window
        if let firstKept = shownAt.firstIndex(where: { $0 > cutoff }) {
            shownAt.removeFirst(firstKept)
        } else {
            shownAt.removeAll()
        }
    }

    /// The frames drawn a second, or nil until a window has gone by to measure over.
    func shownPerSecond(now: Double) -> Double? {
        guard let startedAt, now - startedAt >= 0.5 else { return nil }
        let cutoff = now - window
        let count = shownAt.filter { $0 > cutoff }.count
        // Before a whole window has passed, the count is over the time there has been.
        let span = min(window, now - startedAt)
        return span > 0 ? Double(count) / span : nil
    }
}

/// What a key of the keyboard does to the playback.
enum TimelineCommand: Equatable {
    case playPause, stepBack, stepForward, goToStart, goToEnd

    /// The command a key stands for, or nil for a key the timeline leaves alone. Space plays, the
    /// arrows step, and option with an arrow goes to an end. Any other modifier means the key is
    /// something else (command and arrows are the text and the window's).
    static func forKey(code: UInt16, option: Bool, otherModifiers: Bool) -> TimelineCommand? {
        guard !otherModifiers else { return nil }
        switch (code, option) {
        case (49, false): return .playPause // space
        case (123, false): return .stepBack // left arrow
        case (124, false): return .stepForward // right arrow
        case (123, true): return .goToStart
        case (124, true): return .goToEnd
        default: return nil
        }
    }
}
