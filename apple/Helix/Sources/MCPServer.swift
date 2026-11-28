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
/// thread safe. The protocol itself is `MCPDispatcher`'s, which the live server in the app shares.
/// When a Helix app is running, `Helix --mcp` does not come here: it relays to that app (see
/// `MCPLaunch`), and this server is what is left for an agent with no app to talk to.
@MainActor
final class MCPServer {
    static let flag = "--mcp"

    private let protocolDescriptor: Int32
    private let dispatcher = MCPDispatcher.headless()

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

    /// Handles one line of input and writes the reply, when there is one. Here a tool runs on the
    /// spot, on the engine of this process.
    private func receive(_ line: String) {
        switch dispatcher.receive(line) {
        case .reply(let reply):
            if let reply { send(reply) }
        case .call(let id, let tool, let arguments):
            send(MCPDispatcher.toolReply(id: id, Result { try tool.handler(arguments) }))
        }
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
