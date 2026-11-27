import Foundation

/// How much geometry the interface lets a single edit ask for without checking, and when it says
/// so after a cook.
///
/// A typo (a zero too many in a grid) makes a mesh the machine cannot hold: a grid of 4000 by
/// 4000 takes ten seconds and four gigabytes. The generators say what they will make from their
/// parameters, so the edit is stopped before the engine starts. The estimate is the number of
/// points, from the parameters alone: cheap, and the same on every machine. Nothing here touches
/// the engine, so the rules are checked in the tests.
enum GeometryLimits {
    /// Above this many points, an edit that makes the geometry larger asks first, and a cook that
    /// leaves the viewport with more says so quietly.
    static let threshold: Double = 5_000_000

    /// The number of points a node of a type makes from its parameters, or nil for a type that
    /// is not a generator of known size (its size depends on its input, or is small).
    /// - Parameter value: reads a parameter of the node, nil when it has none.
    static func estimatedPoints(type: String, value: (String) -> Double?) -> Double? {
        func product(_ names: String...) -> Double? {
            var result = 1.0
            for name in names {
                guard let number = value(name), number.isFinite else { return nil }
                result *= max(number, 0)
            }
            return result
        }
        switch type {
        case "helix::grid", "helix::sphere", "helix::cylinder", "helix::torus":
            return product("rows", "columns")
        case "helix::circle":
            return product("divisions")
        case "helix::line":
            return product("points")
        case "helix::spiral":
            return product("turns", "pointsPerTurn")
        default:
            return nil
        }
    }

    /// The size an edit would make a node of a type, when it is worth asking about: above the
    /// threshold and larger than the node makes now (an edit that brings a huge mesh down is
    /// never held back).
    /// - Parameters:
    ///   - overrides: the values the edit writes, by parameter name.
    ///   - current: the values the node holds now.
    static func largeEstimate(type: String, overrides: [String: Double], current: (String) -> Double?) -> Double? {
        guard let after = estimatedPoints(type: type, value: { overrides[$0] ?? current($0) }),
              after > threshold else { return nil }
        let before = estimatedPoints(type: type, value: current) ?? 0
        return after > before ? after : nil
    }

    /// Whether a cook that left this many points on screen is worth a quiet word.
    static func isHeavy(pointCount: UInt64) -> Bool {
        Double(pointCount) > threshold
    }

    /// A count of points in whole millions, which is how the messages say it ("about 16 million").
    /// A value typed in the inspector can be as large as 1e30, and converting that to Int would stop
    /// the app while it asks whether to go on: the count is capped at a million million points,
    /// which still reads as far too many, and a count that is not a number says the cap.
    static func millions(_ points: Double) -> Int {
        let millions = (points / 1_000_000).rounded()
        guard millions.isFinite else { return maximumMillions }
        return Int(min(max(millions, 0), Double(maximumMillions)))
    }

    /// The most the messages say, in millions.
    static let maximumMillions = 1_000_000
}
