import CoreGraphics
import Foundation

/// How numbers are written in a field and read back from it.
enum NumberText {
    /// The most digits after the point a field shows. The value itself keeps its precision.
    static let fractionDigits = 4

    /// Writes a number the way a field shows it: no trailing zeros, no "-0", and a magnitude
    /// that would need a long run of digits in exponent form.
    static func format(_ value: Double) -> String {
        guard value.isFinite else { return value.isNaN ? "nan" : (value < 0 ? "-inf" : "inf") }
        if value == 0 { return "0" }
        if abs(value) >= 1e9 || abs(value) < 1e-4 {
            return String(format: "%g", value)
        }
        var text = String(format: "%.\(fractionDigits)f", value)
        if text.contains(".") {
            while text.hasSuffix("0") { text.removeLast() }
            if text.hasSuffix(".") { text.removeLast() }
        }
        // A value that rounds to zero at four digits reads "-0".
        return text == "-0" ? "0" : text
    }

    static func format(_ value: Int) -> String { String(value) }

    /// What reading a field came to.
    enum Parsed<Value: Equatable>: Equatable {
        case value(Value)
        /// The text is not a number the parameter can hold, with why, in words for a person.
        case invalid(String)
    }

    /// Reads a float from a field. Accepts a comma as the decimal separator when there is no
    /// point ("1,5"), and an exponent ("1e3"). Refuses what is not a finite number rather than
    /// reading it as zero.
    static func parseFloat(_ text: String) -> Parsed<Double> {
        var trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty { return .invalid(String(localized: "Enter a number.")) }
        if trimmed.filter({ $0 == "," }).count == 1, !trimmed.contains(".") {
            trimmed = trimmed.replacingOccurrences(of: ",", with: ".")
        }
        // Double(_:) also reads "nan", "inf" and hexadecimal floats, which no one means here.
        let allowed = Set("0123456789+-.eE")
        guard trimmed.allSatisfy({ allowed.contains($0) }), let value = Double(trimmed), value.isFinite else {
            return .invalid(String(localized: "Enter a number."))
        }
        return .value(value)
    }

    /// Reads a whole number. A number with a fraction is refused, not rounded.
    static func parseInt(_ text: String) -> Parsed<Int> {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty { return .invalid(String(localized: "Enter a whole number.")) }
        if let value = Int(trimmed) { return .value(value) }
        if case .value(let number) = parseFloat(trimmed), number == number.rounded(), abs(number) < 9e15 {
            return .value(Int(number))
        }
        return .invalid(String(localized: "Enter a whole number."))
    }
}

/// How a slider position maps onto a value, with the range of the parameter.
enum SliderMapping {
    /// Where a value sits on the slider, from 0 to 1. A value past an unlocked end pins the thumb
    /// at that end, since the slider cannot show it.
    static func fraction(of value: Double, in range: InspectorRange) -> Double {
        guard range.hasSlider else { return 0 }
        return min(max((value - range.min) / (range.max - range.min), 0), 1)
    }

    /// The value at a slider position from 0 to 1, held to the locked ends (which the slider
    /// ends are, so this never moves a value that was already inside them).
    static func value(atFraction fraction: Double, in range: InspectorRange, integer: Bool = false) -> Double {
        let raw = range.min + min(max(fraction, 0), 1) * (range.max - range.min)
        return range.clamped(integer ? raw.rounded() : raw)
    }
}

/// How dragging over a label changes a value.
enum Scrub {
    /// Points of drag that make one full range of a slider.
    static let pointsPerRange: Double = 300
    /// Points of drag for one step of an integer.
    static let pointsPerStep: Double = 6
    /// A fine drag, with the option key down, moves this much slower.
    static let fineFactor: Double = 0.1

    /// The value a drag of `translation` points gives, from the value where it started.
    ///
    /// A parameter with a range scrubs across it as a slider would. One with no usable range
    /// (the engine's default 0 to 10 aside, a node may not give one) moves in proportion to its
    /// own size, with a floor, so any value can be brought anywhere. The result is held to the
    /// locked ends and never leaves finite numbers.
    static func value(
        from start: Double,
        translation: CGFloat,
        range: InspectorRange?,
        integer: Bool,
        fine: Bool = false
    ) -> Double {
        guard translation.isFinite else { return start }
        let factor = fine ? fineFactor : 1
        var delta: Double
        if integer {
            delta = (Double(translation) / pointsPerStep * factor).rounded(.towardZero)
        } else if let range, range.hasSlider {
            delta = Double(translation) / pointsPerRange * (range.max - range.min) * factor
        } else {
            delta = Double(translation) * max(abs(start) * 0.01, 0.01) * factor
        }
        var result = start + delta
        if integer { result = result.rounded() }
        if let range { result = range.clamped(result) }
        return result.isFinite ? result : start
    }
}

/// What a row of the inspector lets a person do, from what the engine says of it now.
struct RowAvailability: Equatable {
    var isHidden: Bool
    /// Dimmed and not interactive: a disable condition holds, or the node is not editable.
    var isDisabled: Bool

    init(parameter: InspectorParameter, nodeEditable: Bool = true) {
        isHidden = parameter.hidden
        isDisabled = !parameter.enabled || !nodeEditable
    }

    /// Whether one component can be edited by hand. One that holds keyframes can: the engine keys
    /// the current frame with what is typed.
    func canEdit(_ parameter: InspectorParameter, component: Int) -> Bool {
        !isHidden && !isDisabled
    }
}

/// The names of the edits, as they appear in the Edit menu next to Undo.
enum InspectorEditName {
    static func change(_ label: String) -> String { String(localized: "Change \(label)") }
    static func reset(_ label: String) -> String { String(localized: "Reset \(label)") }
    static var editExpression: String { String(localized: "Edit Expression") }
    static var clearExpression: String { String(localized: "Clear Expression") }
    static var setKeyframe: String { String(localized: "Set Keyframe") }
    static var removeKeyframe: String { String(localized: "Remove Keyframe") }
    static var clearKeyframes: String { String(localized: "Clear Keyframes") }
}

/// The error of an expression, cut down to what a row has room for.
enum ExpressionErrorText {
    /// The first line of what the expression engine says, without its "error[30151]: " code.
    /// The rest (the file, the line, a copy of the source) is for the log.
    static func short(_ message: String) -> String {
        var line = message.split(whereSeparator: \.isNewline).first.map(String.init) ?? message
        if line.hasPrefix("error["), let close = line.firstIndex(of: "]") {
            line = String(line[line.index(after: close)...]).trimmingCharacters(in: CharacterSet(charactersIn: ": "))
        }
        return line.count > 160 ? String(line.prefix(160)) + "…" : line
    }
}
