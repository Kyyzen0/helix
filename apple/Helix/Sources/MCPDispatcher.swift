import Foundation

/// The JSON-RPC side of the Model Context Protocol, shared by the two servers: the headless one
/// on standard input and output (`MCPServer`) and the live one in the app (`AgentLink`).
///
/// The dispatcher reads one line and says what it comes to: a reply to write now, or a tool to
/// call. It never calls a tool itself, since the two servers do that differently: the headless
/// one on the spot, the live one inside an undo step of the open window, and possibly after
/// waiting for the person. It is one per conversation: it remembers who the client said it was.
@MainActor
final class MCPDispatcher {
    /// Where the tools act: on an engine of the process that nobody sees, or on the scene open in
    /// a window of the app.
    enum Context {
        case headless
        case live
    }

    /// What a line comes to.
    enum Step {
        /// A reply to write, or nil for a notification and for the reply of a client, which get none.
        case reply(JSONValue?)
        /// A tool to call; `MCPDispatcher.toolReply` makes the answer from what it came to.
        case call(id: JSONValue, tool: MCPTool, arguments: JSONValue)
    }

    /// The protocol versions the server speaks, newest first. A client that asks for one of them
    /// gets it, and any other gets the second.
    static let supportedVersions = ["2025-11-25", "2025-06-18", "2025-03-26"]
    static let fallbackVersion = "2025-06-18"

    static let instructions = """
        Helix is a procedural 3D tool: a scene is a graph of nodes. Work in this order: \
        list_node_types (it gives the input and output ports of each type), create_node for each \
        step, connect_nodes to wire them, set_parameter to adjust a node, and set_display_node to \
        say which node's output is the result. Then check it: geometry_summary gives the counts, bounds and attributes as data, \
        read_attribute gives values, and render_view gives a picture. Nodes are named by name, \
        path or id. A failed call returns isError with a message: read it and retry.
        """

    /// What a live server adds: the scene is the one a person has open, and sees.
    static let liveInstructions = """
         You are editing the scene open in the Helix window, live: the person sees every step as \
        you make it, and each call that changes the scene is one step of their Undo. Do not \
        replace their scene unasked: new_scene and open_scene ask them first and may wait for the \
        answer. If no scene is open, ask the person to open or create one.
        """

    let context: Context
    private let tools: [MCPTool]
    private let toolsByName: [String: MCPTool]
    private let serverVersion: String

    /// The name the client gave in `initialize`, as it wrote it, or nil before it did.
    private(set) var clientName: String?
    /// Called once the client has said who it is, which a window shows.
    var onInitialize: ((String?) -> Void)?

    init(context: Context, tools: [MCPTool], serverVersion: String) {
        self.context = context
        self.tools = tools
        self.serverVersion = serverVersion
        toolsByName = Dictionary(uniqueKeysWithValues: tools.map { ($0.name, $0) })
    }

    // MARK: Reading a line

    /// Handles one line of input.
    func receive(_ line: String) -> Step {
        let trimmed = line.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return .reply(nil) }
        guard let message = JSONValue.parse(trimmed) else {
            return .reply(Self.failure(id: .null, code: -32700, message: "The message is not valid JSON."))
        }
        guard let fields = message.object, fields["method"]?.string != nil else {
            // A response to a request of the server, which it never makes, is ignored.
            if message.object?["result"] != nil || message.object?["error"] != nil { return .reply(nil) }
            return .reply(Self.failure(id: message["id"] ?? .null, code: -32600, message: "The message is not a request."))
        }
        return handle(fields)
    }

    private func handle(_ fields: [String: JSONValue]) -> Step {
        let method = fields["method"]?.string ?? ""
        let params = fields["params"]
        guard let id = fields["id"], !id.isNull else { return .reply(nil) }

        switch method {
        case "initialize":
            return .reply(Self.success(id: id, result: initialize(params)))
        case "ping":
            return .reply(Self.success(id: id, result: [:]))
        case "tools/list":
            return .reply(Self.success(id: id, result: ["tools": .array(tools.map(\.listing))]))
        case "tools/call":
            return prepareCall(id: id, params: params)
        default:
            return .reply(Self.failure(id: id, code: -32601, message: "Unknown method: \(method)."))
        }
    }

    private func initialize(_ params: JSONValue?) -> JSONValue {
        let asked = params?["protocolVersion"]?.string ?? ""
        let version = Self.supportedVersions.contains(asked) ? asked : Self.fallbackVersion
        clientName = params?["clientInfo"]?["name"]?.string
        onInitialize?(clientName)
        return [
            "protocolVersion": .string(version),
            "capabilities": ["tools": [:]],
            "serverInfo": [
                "name": "helix",
                "version": .string(serverVersion),
            ],
            "instructions": .string(context == .live ? Self.instructions + Self.liveInstructions : Self.instructions),
        ]
    }

    private func prepareCall(id: JSONValue, params: JSONValue?) -> Step {
        guard let name = params?["name"]?.string else {
            return .reply(Self.failure(id: id, code: -32602, message: "tools/call needs the name of a tool."))
        }
        guard let tool = toolsByName[name] else {
            return .reply(Self.failure(id: id, code: -32602, message: "Unknown tool: \(name)."))
        }
        let arguments = params?["arguments"] ?? [:]
        guard arguments.object != nil || arguments.isNull else {
            return .reply(Self.failure(id: id, code: -32602, message: "The arguments of a tool are an object."))
        }
        return .call(id: id, tool: tool, arguments: arguments.isNull ? [:] : arguments)
    }

    // MARK: Replies

    /// The answer to a tool call, from what the tool came to. A refusal the agent can read and
    /// correct is a result with `isError`, not a protocol error.
    static func toolReply(id: JSONValue, _ outcome: Result<MCPToolOutput, Error>) -> JSONValue {
        switch outcome {
        case .success(let output):
            var result: [String: JSONValue] = ["content": .array(output.content)]
            if let structured = output.structured { result["structuredContent"] = structured }
            return success(id: id, result: .object(result))
        case .failure(let error as MCPToolError):
            return success(id: id, result: toolFailure(error.message))
        case .failure(let error):
            return success(id: id, result: toolFailure(error.localizedDescription))
        }
    }

    /// The result of a tool that failed: the agent reads the message and can try again.
    static func toolFailure(_ message: String) -> JSONValue {
        ["content": [["type": "text", "text": .string(message)]], "isError": true]
    }

    static func success(id: JSONValue, result: JSONValue) -> JSONValue {
        ["jsonrpc": "2.0", "id": id, "result": result]
    }

    static func failure(id: JSONValue, code: Int64, message: String) -> JSONValue {
        ["jsonrpc": "2.0", "id": id, "error": ["code": .int(code), "message": .string(message)]]
    }
}
