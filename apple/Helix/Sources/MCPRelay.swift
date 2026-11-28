import Foundation

/// What `Helix --mcp` does: relay to the Helix that is running, or serve on its own.
///
/// An AI app starts `Helix --mcp` as the server of its configuration. If a Helix is open, the
/// agent should edit that one, in its window: this process then does no work of its own. It passes
/// the lines of standard input to the app's socket and the lines of the socket to standard output,
/// with no engine and no AppKit, and nothing else on standard output. With no app running there is
/// no scene to edit, and the headless server of today answers.
///
/// `--headless` serves on its own even when an app is running (the smoke test does, so that it
/// never edits the scene of the person running it). `--live` needs the app, and says so when it is
/// not running, instead of falling back to an invisible scene the agent might take for the real one.
enum MCPLaunch {
    static let headlessFlag = "--headless"
    static let liveFlag = "--live"

    static let usage = "usage: Helix --mcp [\(headlessFlag) | \(liveFlag)]"

    enum Mode: Equatable {
        /// The app if it is running, the headless server otherwise.
        case automatic
        case headless
        case live
    }

    /// The mode the arguments ask for, or nil when they contradict each other or hold a flag nobody knows.
    static func mode(of arguments: [String]) -> Mode? {
        let headless = arguments.contains(headlessFlag)
        let live = arguments.contains(liveFlag)
        if headless && live { return nil }
        return headless ? .headless : live ? .live : .automatic
    }

    @MainActor
    static func run(_ arguments: [String]) -> Int32 {
        guard let mode = mode(of: arguments) else {
            log(usage)
            return 2
        }
        if mode != .headless {
            let path = AgentSocket.current
            if let descriptor = AgentSocket.connect(to: path) {
                return MCPRelay.pump(descriptor)
            }
            if mode == .live {
                log("no running Helix answers at \(path). Open Helix, and check Settings > AI Agents, or leave out \(liveFlag).")
                return 1
            }
        }
        return MCPServer.run()
    }

    private static func log(_ message: String) {
        FileHandle.standardError.write(Data("Helix MCP: \(message)\n".utf8))
    }
}

/// Passes lines between standard input and standard output and the socket of the app, until one
/// side closes.
enum MCPRelay {
    /// Runs the relay and returns the exit code. The descriptor is a connection to the app.
    static func pump(_ socket: Int32) -> Int32 {
        // A client that goes away must end the loop with an error to read, not kill the process.
        signal(SIGPIPE, SIG_IGN)
        // The app to the client. When the app closes, the relay is over, whatever the client is
        // doing: it may be waiting for input that never comes.
        let reader = Thread {
            var chunk = [UInt8](repeating: 0, count: 64 * 1024)
            while true {
                let count = read(socket, &chunk, chunk.count)
                if count > 0 {
                    if !AgentSocket.writeAll(STDOUT_FILENO, Array(chunk[0..<count])) { break }
                } else if count < 0 && errno == EINTR {
                    continue
                } else {
                    break
                }
            }
            exit(0)
        }
        reader.stackSize = 1 << 20
        reader.start()

        // The client to the app. At the end of its input, the app is told, answers what it still
        // owes and closes, which ends the reader.
        var chunk = [UInt8](repeating: 0, count: 64 * 1024)
        while true {
            let count = read(STDIN_FILENO, &chunk, chunk.count)
            if count > 0 {
                if !AgentSocket.writeAll(socket, Array(chunk[0..<count])) { break }
            } else if count < 0 && errno == EINTR {
                continue
            } else {
                shutdown(socket, SHUT_WR)
                break
            }
        }
        // Whatever the app still owes is on its way: the reader ends the process when it closes.
        while true { sleep(3600) }
    }
}
