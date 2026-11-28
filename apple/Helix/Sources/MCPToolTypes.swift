import Foundation

/// What a tool hands back: the content the agent reads, and the same data as a JSON object.
struct MCPToolOutput {
    var content: [JSONValue]
    var structured: JSONValue?

    /// A result that is data: the object goes out as structured content and, as text, with the
    /// same JSON, for the clients that read only the text.
    static func data(_ value: JSONValue) -> MCPToolOutput {
        MCPToolOutput(content: [["type": "text", "text": .string(value.serialized())]], structured: value)
    }
}

/// A tool that failed in a way the agent can read and correct: a node that does not exist, a
/// value out of range, the engine refusing. It becomes a result with `isError`, not a protocol
/// error.
struct MCPToolError: Error {
    let message: String
    init(_ message: String) { self.message = message }
}

/// One tool of the server: how it is announced, and what it does.
struct MCPTool {
    let name: String
    let summary: String
    let inputSchema: JSONValue
    let annotations: JSONValue
    let handler: @MainActor (JSONValue) throws -> MCPToolOutput

    /// The entry of `tools/list`.
    var listing: JSONValue {
        [
            "name": .string(name),
            "description": .string(summary),
            "inputSchema": inputSchema,
            "annotations": annotations,
        ]
    }
}

/// The arguments of a call, read with the errors an agent can act on.
struct Arguments {
    let values: JSONValue

    func has(_ key: String) -> Bool {
        if let value = values[key] { return !value.isNull }
        return false
    }

    func value(_ key: String) -> JSONValue? {
        has(key) ? values[key] : nil
    }

    func requiredString(_ key: String) throws -> String {
        guard let value = value(key) else { throw MCPToolError("The argument '\(key)' is required.") }
        guard let text = value.string else { throw MCPToolError("The argument '\(key)' must be a string.") }
        return text
    }

    func string(_ key: String) throws -> String? {
        guard let value = value(key) else { return nil }
        guard let text = value.string else { throw MCPToolError("The argument '\(key)' must be a string.") }
        return text
    }

    func number(_ key: String) throws -> Double? {
        guard let value = value(key) else { return nil }
        guard let number = value.number else { throw MCPToolError("The argument '\(key)' must be a number.") }
        return number
    }

    func requiredNumber(_ key: String) throws -> Double {
        guard let number = try number(key) else { throw MCPToolError("The argument '\(key)' is required.") }
        return number
    }

    func integer(_ key: String) throws -> Int64? {
        guard let value = value(key) else { return nil }
        guard let integer = value.integer else { throw MCPToolError("The argument '\(key)' must be an integer.") }
        return integer
    }

    func bool(_ key: String) throws -> Bool? {
        guard let value = value(key) else { return nil }
        guard let flag = value.bool else { throw MCPToolError("The argument '\(key)' must be true or false.") }
        return flag
    }
}
