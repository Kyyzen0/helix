import Foundation

/// The state of an edit that goes on for a while, such as dragging a slider: the scene is
/// captured when it starts and, when it ends, one step is registered if the scene is not what it
/// was. A drag that comes back to where it started registers nothing, and so does one that
/// is cancelled.
///
/// It holds no engine and no undo manager, so the sequence of begin, update and end can be tested
/// on its own.
struct ContinuousEdit: Equatable {
    /// What registering the step takes: its name and the scene before the gesture.
    struct Registration: Equatable {
        var name: String
        var before: String
    }

    private(set) var active: Registration?

    var isActive: Bool { active != nil }

    /// Starts a gesture. Returns false, changing nothing, when one is already going on, so the
    /// caller knows it does not own the end.
    @discardableResult
    mutating func begin(named name: String, before: String) -> Bool {
        guard active == nil else { return false }
        active = Registration(name: name, before: before)
        return true
    }

    /// Ends the gesture. Returns what to register on the undo manager, or nil when the scene is
    /// what it was before, or when no gesture was going on.
    mutating func end(after: String?) -> Registration? {
        guard let started = active else { return nil }
        active = nil
        guard let after, after != started.before else { return nil }
        return started
    }

    /// Abandons the gesture. Returns the scene to put back, or nil when none was going on.
    mutating func cancel() -> String? {
        guard let started = active else { return nil }
        active = nil
        return started.before
    }
}

/// Decides how often a live edit is applied to the scene while a gesture goes on.
///
/// Applying a value means a cook and a new upload of the geometry, which costs a few milliseconds
/// for a model of the size the viewport is made for. As long as it fits in `budget` every change
/// is applied at once. When a scene is heavy enough that it does not, the changes that come while
/// the last one was still being paid for are held, and only the newest is applied when the cost
/// has passed, so the pointer stays ahead of the cook rather than behind it.
struct LiveUpdateGate: Equatable {
    /// The cost, in seconds, above which changes start to be held back.
    var budget: Double = 0.030

    private(set) var lastCost: Double = 0
    private(set) var lastEnd: Double?

    enum Decision: Equatable {
        case applyNow
        /// Hold the change, and try again at this time (seconds, on the clock of the caller).
        case holdUntil(Double)
    }

    /// What to do with a change that arrives at `now`.
    func offer(now: Double) -> Decision {
        guard lastCost > budget, let lastEnd else { return .applyNow }
        let ready = lastEnd + lastCost
        return now >= ready ? .applyNow : .holdUntil(ready)
    }

    /// Notes that a change was applied, from `start` to `end`.
    mutating func applied(start: Double, end: Double) {
        lastCost = max(end - start, 0)
        lastEnd = end
    }

    /// Forgets the cost, for a new gesture.
    mutating func reset() {
        lastCost = 0
        lastEnd = nil
    }
}
