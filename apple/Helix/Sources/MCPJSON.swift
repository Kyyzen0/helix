import Foundation

/// A JSON value, which is what the Model Context Protocol speaks.
///
/// Swift 6 does not let `[String: Any]` cross a concurrency boundary, and a protocol message has
/// a fixed set of shapes anyway, so the server holds its messages in this enum.
enum JSONValue: Sendable, Equatable {
    case null
    case bool(Bool)
    case int(Int64)
    case double(Double)
    case string(String)
    case array([JSONValue])
    case object([String: JSONValue])

    // MARK: Reading

    var string: String? {
        if case .string(let value) = self { return value }
        return nil
    }

    var bool: Bool? {
        if case .bool(let value) = self { return value }
        return nil
    }

    var array: [JSONValue]? {
        if case .array(let value) = self { return value }
        return nil
    }

    var object: [String: JSONValue]? {
        if case .object(let value) = self { return value }
        return nil
    }

    /// Any number, whether it was written as an integer or not.
    var number: Double? {
        switch self {
        case .int(let value): return Double(value)
        case .double(let value): return value
        default: return nil
        }
    }

    /// A number with no fraction (3 and 3.0 both are), which fits in 64 bits.
    var integer: Int64? {
        switch self {
        case .int(let value):
            return value
        case .double(let value):
            guard value.rounded() == value, abs(value) < 9.0e18 else { return nil }
            return Int64(value)
        default:
            return nil
        }
    }

    var isNull: Bool {
        if case .null = self { return true }
        return false
    }

    subscript(key: String) -> JSONValue? {
        object?[key]
    }

    // MARK: Parsing

    /// Parses one JSON document, or returns nil when the text is not valid JSON.
    static func parse(_ text: String) -> JSONValue? {
        guard let data = text.data(using: .utf8),
              let any = try? JSONSerialization.jsonObject(with: data, options: [.fragmentsAllowed])
        else { return nil }
        return convert(any)
    }

    private static func convert(_ any: Any) -> JSONValue {
        switch any {
        case is NSNull:
            return .null
        case let number as NSNumber:
            if CFGetTypeID(number) == CFBooleanGetTypeID() { return .bool(number.boolValue) }
            if CFNumberIsFloatType(number) { return .double(number.doubleValue) }
            return .int(number.int64Value)
        case let text as String:
            return .string(text)
        case let items as [Any]:
            return .array(items.map(convert))
        case let fields as [String: Any]:
            return .object(fields.mapValues(convert))
        default:
            return .null
        }
    }

    // MARK: Writing

    /// The compact JSON text of the value, with the keys in order so the same value always
    /// gives the same text. A number that is not finite becomes null, since JSON has none.
    func serialized() -> String {
        var output = ""
        write(to: &output)
        return output
    }

    private func write(to output: inout String) {
        switch self {
        case .null:
            output += "null"
        case .bool(let value):
            output += value ? "true" : "false"
        case .int(let value):
            output += String(value)
        case .double(let value):
            output += value.isFinite ? String(value) : "null"
        case .string(let value):
            Self.writeString(value, to: &output)
        case .array(let items):
            output += "["
            for (index, item) in items.enumerated() {
                if index > 0 { output += "," }
                item.write(to: &output)
            }
            output += "]"
        case .object(let fields):
            output += "{"
            for (index, key) in fields.keys.sorted().enumerated() {
                if index > 0 { output += "," }
                Self.writeString(key, to: &output)
                output += ":"
                fields[key]?.write(to: &output)
            }
            output += "}"
        }
    }

    private static func writeString(_ value: String, to output: inout String) {
        output += "\""
        for scalar in value.unicodeScalars {
            switch scalar {
            case "\"": output += "\\\""
            case "\\": output += "\\\\"
            case "\n": output += "\\n"
            case "\r": output += "\\r"
            case "\t": output += "\\t"
            default:
                if scalar.value < 0x20 {
                    output += String(format: "\\u%04x", scalar.value)
                } else {
                    output.unicodeScalars.append(scalar)
                }
            }
        }
        output += "\""
    }
}

extension JSONValue: ExpressibleByStringLiteral {
    init(stringLiteral value: String) { self = .string(value) }
}

extension JSONValue: ExpressibleByIntegerLiteral {
    init(integerLiteral value: Int64) { self = .int(value) }
}

extension JSONValue: ExpressibleByBooleanLiteral {
    init(booleanLiteral value: Bool) { self = .bool(value) }
}

extension JSONValue: ExpressibleByFloatLiteral {
    init(floatLiteral value: Double) { self = .double(value) }
}

extension JSONValue: ExpressibleByArrayLiteral {
    init(arrayLiteral elements: JSONValue...) { self = .array(elements) }
}

extension JSONValue: ExpressibleByDictionaryLiteral {
    init(dictionaryLiteral elements: (String, JSONValue)...) {
        self = .object(Dictionary(elements, uniquingKeysWith: { _, last in last }))
    }
}

extension JSONValue {
    /// A number from a float of the engine, which is what its values are. It goes through the
    /// shortest text that reads back as the same float, so 0.1 stays 0.1 and does not become
    /// 0.10000000149011612.
    init(_ value: Float) { self = .double(Double(String(value)) ?? Double(value)) }

    /// A number from an unsigned count.
    init(count value: UInt64) { self = .int(Int64(clamping: value)) }
}
