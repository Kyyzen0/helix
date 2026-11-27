import Foundation

// The inspector works on plain values, copied out of the engine, like the node editor does.
// Nothing in the files that format, map, group or sequence values imports the facade, so all of
// it can be tested without an engine.

/// What a parameter holds, as the facade classifies it.
enum InspectorKind: Equatable, Sendable {
    case float, int, string, toggle, dropdown, ramp, other
}

/// How a node asks for a parameter to be drawn.
enum InspectorStyle: Equatable, Sendable {
    case plain, xyz, rangeCircle, boolIcon, boolSwitch, attribute, code

    /// The style a node.yaml name stands for. An unknown name draws the plain widget.
    init(name: String) {
        switch name {
        case "xyz": self = .xyz
        case "rangeCircle": self = .rangeCircle
        case "boolIcon": self = .boolIcon
        case "boolSwitch": self = .boolSwitch
        case "attribute": self = .attribute
        case "code": self = .code
        default: self = .plain
        }
    }
}

/// The range a numeric parameter declares. An unlocked end is only where a slider stops, and a
/// person may type past it. A locked end is a limit.
struct InspectorRange: Equatable, Sendable {
    var min: Double
    var max: Double
    var minLocked: Bool
    var maxLocked: Bool

    init(min: Double, max: Double, minLocked: Bool = false, maxLocked: Bool = false) {
        self.min = min
        self.max = max
        self.minLocked = minLocked
        self.maxLocked = maxLocked
    }

    /// Whether a slider can be drawn over the range at all.
    var hasSlider: Bool { max > min && min.isFinite && max.isFinite }

    /// The value held to the locked ends. An unlocked end never limits a value.
    func clamped(_ value: Double) -> Double {
        if minLocked, value < min { return min }
        if maxLocked, value > max { return max }
        return value
    }

    /// The same for an integer parameter. The locked ends are rounded inward, as the engine does,
    /// so the result is inside the range.
    func clamped(_ value: Int) -> Int {
        if minLocked, Double(value) < min { return Int(min.rounded(.up)) }
        if maxLocked, Double(value) > max { return Int(max.rounded(.down)) }
        return value
    }
}

struct InspectorOption: Equatable, Hashable, Sendable {
    var value: String
    var label: String
}

struct InspectorRampPoint: Equatable, Sendable {
    var position: Double
    var value: Double
    var interpolation: String
}

/// One parameter of the node being inspected, with its current values.
struct InspectorParameter: Equatable, Identifiable, Sendable {
    var name: String
    var label: String
    var tooltip = ""
    var documentation = ""
    var kind: InspectorKind = .other
    var style: InspectorStyle = .plain
    var styleOptions: [String: String] = [:]
    var componentCount = 1
    var range: InspectorRange?
    var options: [InspectorOption] = []
    var labelHidden = false
    /// Whether the parameter, or a group around it, is hidden now.
    var hidden = false
    /// Whether the parameter, and every group around it, is enabled now.
    var enabled = true
    var group = ""

    // The current values, in the list the kind reads, one per component.
    var floats: [Double] = []
    var ints: [Int] = []
    var strings: [String] = []

    var defaultFloats: [Double] = []
    var defaultInts: [Int] = []
    var defaultStrings: [String] = []

    /// The expression driving each component, empty where there is none.
    var expressions: [String] = []
    var expressionErrors: [String] = []
    /// The components that hold keyframes.
    var animated: Set<Int> = []

    var rampPoints: [InspectorRampPoint] = []
    var rampInterpolations: [InspectorOption] = []

    var id: String { name }

    func hasExpression(_ component: Int) -> Bool {
        component < expressions.count && !expressions[component].isEmpty
    }

    func expressionError(_ component: Int) -> String? {
        guard component < expressionErrors.count, !expressionErrors[component].isEmpty else { return nil }
        return expressionErrors[component]
    }

    func isAnimated(_ component: Int) -> Bool { animated.contains(component) }

    /// Whether any component has an expression.
    var hasAnyExpression: Bool { expressions.contains { !$0.isEmpty } }

    /// Whether the parameter can take an expression. A dropdown takes one of its options and a
    /// ramp has points.
    var takesExpression: Bool {
        switch kind {
        case .float, .int, .toggle, .string: true
        case .dropdown, .ramp, .other: false
        }
    }

    /// Whether every component holds its default and none has an expression, so a reset would
    /// change nothing.
    var isAtDefault: Bool {
        if hasAnyExpression { return false }
        switch kind {
        case .float: return floats == defaultFloats
        case .int, .toggle: return ints == defaultInts
        case .string, .dropdown: return strings == defaultStrings
        case .ramp, .other: return false
        }
    }
}

/// One entry of the order the parameters are shown in.
struct InspectorLayoutItem: Equatable, Sendable {
    enum Kind: Equatable, Sendable { case parameter, groupBegin, groupEnd, divider }
    var kind: Kind
    var name: String
    var label = ""
    var icon = ""
    var horizontal = false
    var labelInline = false
    var hidden = false
    var enabled = true
}

/// A node as the inspector shows it.
struct InspectorNode: Equatable, Sendable {
    var id: UInt64
    var name: String
    var path: String
    /// The name of the type, such as "Attribute Noise".
    var typeLabel: String
    var isDisplay: Bool
    var error: String?
    var warnings: [String] = []
    var layout: [InspectorLayoutItem] = []
    var parameters: [String: InspectorParameter] = [:]
}

/// What the inspector has to show.
enum InspectorContent: Equatable, Sendable {
    case none
    case many(Int)
    case node(InspectorNode)
}

// MARK: The tree the view draws

/// A run of members of a group.
struct InspectorGroup: Equatable, Identifiable, Sendable {
    var name: String
    var label: String
    var horizontal: Bool
    var labelInline: Bool
    var enabled: Bool
    var children: [InspectorEntry]
    var id: String { name }
}

/// What a row of the inspector is.
indirect enum InspectorEntry: Equatable, Identifiable, Sendable {
    case parameter(InspectorParameter)
    case group(InspectorGroup)
    case divider(name: String, label: String, icon: String)

    var id: String {
        switch self {
        case .parameter(let parameter): "p:" + parameter.name
        case .group(let group): "g:" + group.name
        case .divider(let name, _, _): "d:" + name
        }
    }
}

enum InspectorTree {
    /// Builds what the view draws from the layout the node declares: groups hold their members,
    /// what a condition hides is left out (and a group with nothing left in it with it), and a
    /// parameter the layout names that the node does not list is skipped rather than invented.
    static func build(layout: [InspectorLayoutItem], parameters: [String: InspectorParameter]) -> [InspectorEntry] {
        var index = 0
        return members(layout, &index, parameters: parameters, closing: false)
    }

    private static func members(
        _ layout: [InspectorLayoutItem],
        _ index: inout Int,
        parameters: [String: InspectorParameter],
        closing: Bool
    ) -> [InspectorEntry] {
        var entries: [InspectorEntry] = []
        while index < layout.count {
            let item = layout[index]
            index += 1
            switch item.kind {
            case .groupEnd:
                if closing { return entries }
            case .groupBegin:
                let children = members(layout, &index, parameters: parameters, closing: true)
                if item.hidden || children.isEmpty { continue }
                entries.append(.group(InspectorGroup(
                    name: item.name, label: item.label, horizontal: item.horizontal,
                    labelInline: item.labelInline, enabled: item.enabled, children: children
                )))
            case .divider:
                if item.hidden { continue }
                entries.append(.divider(name: item.name, label: item.label, icon: item.icon))
            case .parameter:
                guard let parameter = parameters[item.name], !parameter.hidden, !item.hidden else { continue }
                entries.append(.parameter(parameter))
            }
        }
        return entries
    }
}

// MARK: Ramps

enum RampPoints {
    /// The points with one more, halfway between the two neighbours with the widest gap, on the
    /// line between them, so that there is room for it and a linear curve is not changed.
    static func adding(to points: [InspectorRampPoint]) -> [InspectorRampPoint] {
        let sorted = points.sorted { $0.position < $1.position }
        guard sorted.count >= 2 else {
            return points + [InspectorRampPoint(position: 1, value: 1, interpolation: "linear")]
        }
        // Between the pair with the widest gap, so there is room for the new point.
        var widest = 0
        for index in 1..<sorted.count
        where sorted[index].position - sorted[index - 1].position > sorted[widest + 1].position - sorted[widest].position {
            widest = index - 1
        }
        let low = sorted[widest], high = sorted[widest + 1]
        let new = InspectorRampPoint(
            position: (low.position + high.position) / 2,
            value: (low.value + high.value) / 2,
            interpolation: low.interpolation
        )
        return points + [new]
    }
}
