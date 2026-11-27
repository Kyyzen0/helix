import CxxStdlib
import Foundation
import HelixCore

/// `Helix --mcp`: a Model Context Protocol server over the facade, on standard input and output.
///
/// An AI agent starts the application with this flag and talks to it in newline delimited
/// JSON-RPC 2.0, one message per line. There is no window. The server runs its own engine, in
/// its own process: it is not the scene open in a window of the app. See `MCPTools.swift` for
/// the tools and `MCPHandlers.swift` for what they do.
///
/// Standard output carries the protocol and nothing else. The engine prints debug output to it,
/// so before the engine starts the real output is saved on another descriptor and standard
/// output is pointed at standard error, where the engine's noise and the logs go.
///
/// Everything runs on the main thread, one message after the other, since the facade is not
/// thread safe.
@MainActor
final class MCPServer {
    static let flag = "--mcp"

    /// The protocol versions the server speaks, newest first. A client that asks for one of them
    /// gets it, and any other gets the second.
    static let supportedVersions = ["2025-11-25", "2025-06-18", "2025-03-26"]
    static let fallbackVersion = "2025-06-18"

    static let instructions = """
        Helix is a procedural 3D tool: a scene is a graph of nodes. Work in this order: \
        list_node_types, create_node for each step, connect_nodes to wire them, \
        set_parameter to adjust a node, and set_display_node to say which node's output is the \
        result. Then check it: geometry_summary gives the counts, bounds and attributes as data, \
        read_attribute gives values, and render_view gives a picture. Nodes are named by name, \
        path or id. A failed call returns isError with a message: read it and retry.
        """

    private let protocolDescriptor: Int32
    private let tools = MCPTools.all
    private lazy var toolsByName = Dictionary(uniqueKeysWithValues: tools.map { ($0.name, $0) })

    private init(protocolDescriptor: Int32) {
        self.protocolDescriptor = protocolDescriptor
    }

    /// Runs the server until standard input closes, and returns the exit code.
    static func run() -> Int32 {
        // Keeps the protocol clear of everything the engine prints: the protocol goes to a copy
        // of the real output, and standard output becomes standard error.
        fflush(stdout)
        // A client that goes away must end the loop with an error to read, not kill the process.
        signal(SIGPIPE, SIG_IGN)
        let descriptor = dup(STDOUT_FILENO)
        guard descriptor >= 0, dup2(STDERR_FILENO, STDOUT_FILENO) >= 0 else {
            log("could not take hold of the standard output")
            return 1
        }
        setvbuf(stdout, nil, _IOLBF, 0)

        do {
            try EngineHost.shared.startEngine()
        } catch {
            log("the engine did not start: \(error.localizedDescription)")
            return 1
        }
        log("ready, engine \(String(engine: helix.core.version()))")

        let server = MCPServer(protocolDescriptor: descriptor)
        while let line = readLine(strippingNewline: true) {
            server.receive(line)
        }
        return 0
    }

    static func log(_ message: String) {
        FileHandle.standardError.write(Data("Helix MCP: \(message)\n".utf8))
    }

    // MARK: Messages

    /// Handles one line of input and writes the reply, when there is one.
    private func receive(_ line: String) {
        let trimmed = line.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        guard let message = JSONValue.parse(trimmed) else {
            send(Self.failure(id: .null, code: -32700, message: "The message is not valid JSON."))
            return
        }
        guard let fields = message.object, fields["method"]?.string != nil else {
            // A response to a request of the server, which it never makes, is ignored.
            if message.object?["result"] != nil || message.object?["error"] != nil { return }
            send(Self.failure(id: message["id"] ?? .null, code: -32600, message: "The message is not a request."))
            return
        }
        if let reply = handle(fields) { send(reply) }
    }

    /// Returns the reply to a request, or nil for a notification, which never gets one.
    private func handle(_ fields: [String: JSONValue]) -> JSONValue? {
        let method = fields["method"]?.string ?? ""
        let params = fields["params"]
        guard let id = fields["id"], !id.isNull else { return nil }

        switch method {
        case "initialize":
            return Self.success(id: id, result: initialize(params))
        case "ping":
            return Self.success(id: id, result: [:])
        case "tools/list":
            return Self.success(id: id, result: ["tools": .array(tools.map(\.listing))])
        case "tools/call":
            return callTool(id: id, params: params)
        default:
            return Self.failure(id: id, code: -32601, message: "Unknown method: \(method).")
        }
    }

    private func initialize(_ params: JSONValue?) -> JSONValue {
        let asked = params?["protocolVersion"]?.string ?? ""
        let version = Self.supportedVersions.contains(asked) ? asked : Self.fallbackVersion
        return [
            "protocolVersion": .string(version),
            "capabilities": ["tools": [:]],
            "serverInfo": [
                "name": "helix",
                "version": .string(String(engine: helix.core.version())),
            ],
            "instructions": .string(Self.instructions),
        ]
    }

    private func callTool(id: JSONValue, params: JSONValue?) -> JSONValue {
        guard let name = params?["name"]?.string else {
            return Self.failure(id: id, code: -32602, message: "tools/call needs the name of a tool.")
        }
        guard let tool = toolsByName[name] else {
            return Self.failure(id: id, code: -32602, message: "Unknown tool: \(name).")
        }
        let arguments = params?["arguments"] ?? [:]
        guard arguments.object != nil || arguments.isNull else {
            return Self.failure(id: id, code: -32602, message: "The arguments of a tool are an object.")
        }

        do {
            let output = try tool.handler(arguments.isNull ? [:] : arguments)
            var result: [String: JSONValue] = ["content": .array(output.content)]
            if let structured = output.structured { result["structuredContent"] = structured }
            return Self.success(id: id, result: .object(result))
        } catch let error as MCPToolError {
            return Self.success(id: id, result: Self.toolFailure(error.message))
        } catch {
            return Self.success(id: id, result: Self.toolFailure(error.localizedDescription))
        }
    }

    /// The result of a tool that failed: the agent reads the message and can try again.
    static func toolFailure(_ message: String) -> JSONValue {
        ["content": [["type": "text", "text": .string(message)]], "isError": true]
    }

    private static func success(id: JSONValue, result: JSONValue) -> JSONValue {
        ["jsonrpc": "2.0", "id": id, "result": result]
    }

    private static func failure(id: JSONValue, code: Int64, message: String) -> JSONValue {
        ["jsonrpc": "2.0", "id": id, "error": ["code": .int(code), "message": .string(message)]]
    }

    // MARK: Output

    /// Writes one message and its newline to the protocol descriptor, all of it.
    private func send(_ message: JSONValue) {
        var bytes = Array(message.serialized().utf8)
        bytes.append(UInt8(ascii: "\n"))
        bytes.withUnsafeBytes { buffer in
            var offset = 0
            while offset < buffer.count {
                let written = write(protocolDescriptor, buffer.baseAddress! + offset, buffer.count - offset)
                if written < 0 {
                    if errno == EINTR { continue }
                    Self.log("could not write a reply: \(String(cString: strerror(errno)))")
                    return
                }
                offset += written
            }
        }
    }
}
