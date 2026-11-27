import Foundation

// The keyframes the timeline shows, as plain values copied out of the engine.

/// How a value moves from a key to the next one.
enum KeyInterpolation: String, CaseIterable, Equatable, Sendable {
    case step, linear, smooth

    var title: String {
        switch self {
        case .step: String(localized: "Step")
        case .linear: String(localized: "Linear")
        case .smooth: String(localized: "Smooth")
        }
    }
}

/// One key of one component of one parameter of a node.
struct TimelineKey: Equatable, Hashable, Sendable {
    /// The name the engine knows the parameter by.
    var parameter: String
    /// The name a person reads, such as "Translate".
    var label: String
    var component: Int
    /// How many components the parameter has, which says whether the key is named by an axis.
    var componentCount: Int
    var frame: Double
    var value: Double
    var interpolation: KeyInterpolation

    /// The component, as the inspector names it: an axis for a vector, nothing for a single value.
    var componentName: String {
        guard componentCount > 1 else { return "" }
        return ["X", "Y", "Z", "W"][safe: component] ?? String(component + 1)
    }

    /// "Translate X", or "Radius" for a parameter of one value.
    var title: String {
        componentName.isEmpty ? label : label + " " + componentName
    }

    /// The track the key belongs to, which two keys of the same frame cannot share.
    var track: TimelineTrackID { TimelineTrackID(parameter: parameter, component: component) }
}

struct TimelineTrackID: Hashable, Sendable {
    var parameter: String
    var component: Int
}

/// The keys of one frame, merged into one diamond on the ruler.
struct TimelineDiamond: Equatable, Identifiable, Sendable {
    var frame: Double
    var keys: [TimelineKey]

    var id: Double { frame }

    /// What the pointer shows on the diamond: one line for each key, with its value and the way it
    /// leaves the key.
    var tooltip: String {
        let lines = keys.map { key in
            "\(key.title): \(NumberText.format(key.value)) (\(key.interpolation.title.lowercased()))"
        }
        let head = String(localized: "Frame \(NumberText.format(frame))")
        return ([head] + lines).joined(separator: "\n")
    }
}

enum TimelineKeys {
    /// The keys of every track, merged by frame and ordered along the ruler. Within a diamond the
    /// keys are in the order of the parameters, then of the components.
    static func merge(_ keys: [TimelineKey]) -> [TimelineDiamond] {
        var byFrame: [Double: [TimelineKey]] = [:]
        for key in keys { byFrame[key.frame, default: []].append(key) }
        return byFrame.keys.sorted().map { frame in
            TimelineDiamond(
                frame: frame,
                keys: byFrame[frame, default: []].sorted {
                    ($0.parameter, $0.component) < ($1.parameter, $1.component)
                }
            )
        }
    }

    /// Why a diamond cannot be moved `delta` frames, or nil when it can. A key cannot land on a
    /// frame where another key of its own track already sits, since the engine never replaces one
    /// by moving, and the diamond moves whole or not at all.
    static func moveRefusal(of diamond: TimelineDiamond, by delta: Double, among all: [TimelineKey]) -> String? {
        guard delta != 0 else { return nil }
        let target = diamond.frame + delta
        let moving = Set(diamond.keys.map(\.track))
        for key in all where key.frame == target && moving.contains(key.track) {
            return String(localized: "There is a keyframe on frame \(NumberText.format(target)) already.")
        }
        return nil
    }

    /// The whole number of frames a drag of `points` along the ruler moves a diamond, held so the
    /// diamond stays inside the range.
    static func delta(
        dragging points: Double, pointsPerFrame: Double, from frame: Double, first: Int, last: Int
    ) -> Double {
        guard pointsPerFrame > 0, points.isFinite else { return 0 }
        let wanted = (points / pointsPerFrame).rounded()
        let target = min(max(frame + wanted, Double(first)), Double(last))
        return target - frame
    }
}
