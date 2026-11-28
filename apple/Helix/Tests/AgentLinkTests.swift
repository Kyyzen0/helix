import Darwin
import XCTest

// The tests of the link between the app and the AI agents: where the socket is, who the clients
// are called, what a person pastes into their AI app, the protocol both servers speak, and the
// socket itself, on a real descriptor.

final class AgentSocketPathTests: XCTestCase {
    private let support = URL(fileURLWithPath: "/Users/someone/Library/Application Support")

    func testTheSocketIsInTheApplicationSupportFolderOfTheApp() {
        let path = AgentSocket.path(applicationSupport: support, bundleIdentifier: "com.kyyzen.helix", uid: 501, environment: [:])
        XCTAssertEqual(path, "/Users/someone/Library/Application Support/com.kyyzen.helix/mcp.sock")
    }

    func testAPathTooLongForASocketFallsBackToAShortOneWithTheUserId() {
        let long = URL(fileURLWithPath: "/Users/" + String(repeating: "a", count: 90) + "/Library/Application Support")
        let path = AgentSocket.path(applicationSupport: long, bundleIdentifier: "com.kyyzen.helix", uid: 501, environment: [:])
        XCTAssertEqual(path, "/tmp/helix-501/mcp.sock")
        XCTAssertTrue(AgentSocket.fits(path))
    }

    func testTheLimitIsTheSizeOfSunPathLessItsTerminator() {
        XCTAssertTrue(AgentSocket.fits("/" + String(repeating: "a", count: AgentSocket.maxPathBytes - 1)))
        XCTAssertFalse(AgentSocket.fits("/" + String(repeating: "a", count: AgentSocket.maxPathBytes)))
        XCTAssertEqual(AgentSocket.maxPathBytes, MemoryLayout.size(ofValue: sockaddr_un().sun_path) - 1)
        XCTAssertFalse(AgentSocket.fits(""))
    }

    func testAnAddressIsMadeOnlyForAPathThatFits() {
        XCTAssertNotNil(AgentSocket.address(of: "/tmp/helix-501/mcp.sock"))
        XCTAssertNil(AgentSocket.address(of: "/" + String(repeating: "a", count: 200)))
    }

    func testTheEnvironmentChoosesThePathWhenItFits() {
        let environment = [AgentSocket.environmentKey: "/tmp/mine.sock"]
        XCTAssertEqual(AgentSocket.path(applicationSupport: support, bundleIdentifier: "x", uid: 1, environment: environment), "/tmp/mine.sock")
        let tooLong = [AgentSocket.environmentKey: "/" + String(repeating: "b", count: 150)]
        XCTAssertEqual(AgentSocket.path(applicationSupport: support, bundleIdentifier: "x", uid: 1, environment: tooLong), "/Users/someone/Library/Application Support/x/mcp.sock")
    }
}

final class AgentClientNameTests: XCTestCase {
    func testTheKnownAppsAreCalledByTheirNames() {
        XCTAssertEqual(AgentClients.displayName(for: "claude-code"), "Claude Code")
        XCTAssertEqual(AgentClients.displayName(for: "claude-ai"), "Claude")
        XCTAssertEqual(AgentClients.displayName(for: "Claude Desktop"), "Claude")
        XCTAssertEqual(AgentClients.displayName(for: "codex-mcp-client"), "Codex")
        XCTAssertEqual(AgentClients.displayName(for: "codex"), "Codex")
        XCTAssertEqual(AgentClients.displayName(for: "gemini-cli-mcp-client"), "Gemini")
        XCTAssertEqual(AgentClients.displayName(for: "Visual Studio Code"), "VS Code")
    }

    func testAShortNameIsOnlyTakenForAWordOfItsOwn() {
        XCTAssertEqual(AgentClients.displayName(for: "zed"), "Zed")
        XCTAssertEqual(AgentClients.displayName(for: "my-zeditor"), "my-zeditor")
        XCTAssertEqual(AgentClients.displayName(for: "glm-client"), "GLM")
        XCTAssertEqual(AgentClients.displayName(for: "englmann"), "englmann")
    }

    func testAnUnknownNameIsShownAsItCame() {
        XCTAssertEqual(AgentClients.displayName(for: "my-agent 2"), "my-agent 2")
    }

    func testANameThatIsMissingOrEmptyIsAnAnonymousAgent() {
        XCTAssertFalse(AgentClients.displayName(for: nil).isEmpty)
        XCTAssertEqual(AgentClients.displayName(for: "  "), AgentClients.displayName(for: nil))
    }

    func testALongNameIsCutAndControlCharactersAreDropped() {
        let long = String(repeating: "x", count: 300)
        XCTAssertLessThanOrEqual(AgentClients.displayName(for: long).count, AgentClients.longestName + 1)
        XCTAssertEqual(AgentClients.displayName(for: "evil\u{1B}[31m\nname"), "evil[31mname")
    }
}

final class AgentSetupTests: XCTestCase {
    private let binary = "/Applications/Helix.app/Contents/MacOS/Helix"

    func testClaudeCodeGetsTheCommandToAdd() {
        XCTAssertEqual(
            AgentSetup.snippet(for: .claudeCode, binary: binary),
            "claude mcp add helix -- /Applications/Helix.app/Contents/MacOS/Helix --mcp"
        )
    }

    func testAPathWithASpaceOrAQuoteIsQuotedForTheShell() {
        XCTAssertEqual(AgentSetup.shellQuoted("/My Apps/Helix"), "'/My Apps/Helix'")
        XCTAssertEqual(AgentSetup.shellQuoted("/it's/Helix"), "'/it'\\''s/Helix'")
        XCTAssertEqual(AgentSetup.snippet(for: .claudeCode, binary: "/My Apps/Helix"), "claude mcp add helix -- '/My Apps/Helix' --mcp")
    }

    func testTheJSONFormsAreTheMcpServersObject() throws {
        for client in [AgentSetup.Client.claudeDesktop, .gemini, .generic] {
            let text = AgentSetup.snippet(for: client, binary: binary)
            let object = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(text.utf8)) as? [String: Any])
            let servers = try XCTUnwrap(object["mcpServers"] as? [String: Any])
            let helix = try XCTUnwrap(servers["helix"] as? [String: Any])
            XCTAssertEqual(helix["command"] as? String, binary)
            XCTAssertEqual(helix["args"] as? [String], ["--mcp"])
            XCTAssertFalse(text.contains("\\/"), "slashes are not escaped")
        }
    }

    func testCodexGetsATableOfTheConfigFile() {
        let text = AgentSetup.snippet(for: .codex, binary: binary)
        XCTAssertEqual(text, "[mcp_servers.helix]\ncommand = \"/Applications/Helix.app/Contents/MacOS/Helix\"\nargs = [\"--mcp\"]\n")
    }

    func testTomlEscapesWhatWouldBreakAString() {
        XCTAssertEqual(AgentSetup.tomlString("a\"b\\c"), "\"a\\\"b\\\\c\"")
        XCTAssertEqual(AgentSetup.tomlString("a\nb"), "\"a\\nb\"")
    }

    func testEveryClientHasATitleAndASnippet() {
        for client in AgentSetup.Client.allCases {
            XCTAssertFalse(client.title.isEmpty)
            XCTAssertTrue(AgentSetup.snippet(for: client, binary: binary).contains("--mcp"))
        }
    }
}

final class AgentActivityTests: XCTestCase {
    @MainActor func testTheListKeepsTheLastTenNewestFirst() {
        let activity = AgentActivity()
        for index in 0..<15 { activity.record(client: "Codex", text: "action \(index)", failed: false) }
        XCTAssertEqual(activity.recent.count, AgentActivity.keptActions)
        XCTAssertEqual(activity.recent.first?.text, "action 14")
        XCTAssertEqual(activity.recent.last?.text, "action 5")
    }

    @MainActor func testItPulsesWhileACallRunsAndOnlyThen() {
        let activity = AgentActivity()
        XCTAssertFalse(activity.isRunning)
        activity.callStarted()
        activity.callStarted()
        XCTAssertTrue(activity.isRunning)
        activity.callEnded(client: "Codex", text: "done", failed: false)
        XCTAssertTrue(activity.isRunning)
        activity.callEnded(client: "Codex", text: "done", failed: true)
        XCTAssertFalse(activity.isRunning)
        activity.callEnded(client: "Codex", text: "extra", failed: false)
        XCTAssertFalse(activity.isRunning)
        XCTAssertEqual(activity.recent.first?.failed, false)
    }

    @MainActor func testConnectedClientsAreNamedOnceEach() {
        let activity = AgentActivity()
        XCTAssertFalse(activity.isConnected)
        let first = AgentClient(id: UUID(), rawName: nil, since: Date())
        let second = AgentClient(id: UUID(), rawName: "codex", since: Date())
        activity.connected(first)
        activity.connected(second)
        activity.renamed(first.id, to: "codex")
        XCTAssertEqual(activity.clientNames, "Codex")
        activity.renamed(first.id, to: "claude-code")
        XCTAssertEqual(activity.clientNames, "Claude Code, Codex")
        activity.disconnected(first.id)
        activity.disconnected(second.id)
        XCTAssertFalse(activity.isConnected)
    }
}

@MainActor
final class MCPDispatcherTests: XCTestCase {
    private func tool(_ name: String, _ run: @escaping @MainActor (JSONValue) throws -> MCPToolOutput = { _ in .data(["ok": true]) }) -> MCPTool {
        MCPTool(name: name, summary: name, inputSchema: ["type": "object"], annotations: [:], handler: run)
    }

    private func dispatcher(_ context: MCPDispatcher.Context = .headless) -> MCPDispatcher {
        MCPDispatcher(context: context, tools: [tool("one"), tool("two")], serverVersion: "9.9")
    }

    private func reply(_ step: MCPDispatcher.Step, file: StaticString = #filePath, line: UInt = #line) throws -> JSONValue {
        guard case .reply(let reply) = step, let reply else {
            XCTFail("a reply was expected", file: file, line: line)
            throw XCTSkip("no reply")
        }
        return reply
    }

    func testInitializeAnswersWithTheVersionTheClientAsksForAndTheNameOfTheServer() throws {
        let answer = try reply(dispatcher().receive(#"{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"codex"}}}"#))
        XCTAssertEqual(answer["result"]?["protocolVersion"]?.string, "2025-03-26")
        XCTAssertEqual(answer["result"]?["serverInfo"]?["name"]?.string, "helix")
        XCTAssertEqual(answer["result"]?["serverInfo"]?["version"]?.string, "9.9")
        XCTAssertEqual(answer["id"], .int(1))
    }

    func testAVersionNobodyKnowsGetsTheFallback() throws {
        let answer = try reply(dispatcher().receive(#"{"id":"a","method":"initialize","params":{"protocolVersion":"1999"}}"#))
        XCTAssertEqual(answer["result"]?["protocolVersion"]?.string, MCPDispatcher.fallbackVersion)
    }

    func testTheNameTheClientGaveIsRemembered() throws {
        let conversation = dispatcher(.live)
        var heard: String?
        conversation.onInitialize = { heard = $0 }
        XCTAssertNil(conversation.clientName)
        _ = conversation.receive(#"{"id":1,"method":"initialize","params":{"clientInfo":{"name":"gemini-cli"}}}"#)
        XCTAssertEqual(conversation.clientName, "gemini-cli")
        XCTAssertEqual(heard, "gemini-cli")
    }

    func testTheHeadlessInstructionsAreWhatTheyWere() throws {
        let answer = try reply(dispatcher().receive(#"{"id":1,"method":"initialize"}"#))
        let text = try XCTUnwrap(answer["result"]?["instructions"]?.string)
        XCTAssertTrue(text.hasPrefix("Helix is a procedural 3D tool: a scene is a graph of nodes."))
        XCTAssertTrue(text.hasSuffix("A failed call returns isError with a message: read it and retry."))
        XCTAssertFalse(text.contains("live"))
    }

    func testTheLiveInstructionsAddWhatTheLiveServerDoes() throws {
        let answer = try reply(dispatcher(.live).receive(#"{"id":1,"method":"initialize"}"#))
        let text = try XCTUnwrap(answer["result"]?["instructions"]?.string)
        XCTAssertTrue(text.hasPrefix(MCPDispatcher.instructions))
        XCTAssertTrue(text.contains("Undo"))
    }

    func testPingAndTheListOfTools() throws {
        let conversation = dispatcher()
        XCTAssertEqual(try reply(conversation.receive(#"{"id":2,"method":"ping"}"#))["result"], [:])
        let tools = try reply(conversation.receive(#"{"id":3,"method":"tools/list"}"#))["result"]?["tools"]?.array
        XCTAssertEqual(tools?.compactMap { $0["name"]?.string }, ["one", "two"])
    }

    func testANotificationGetsNoReply() throws {
        guard case .reply(let reply) = dispatcher().receive(#"{"method":"notifications/initialized"}"#) else { return XCTFail() }
        XCTAssertNil(reply)
        guard case .reply(let blank) = dispatcher().receive("   ") else { return XCTFail() }
        XCTAssertNil(blank)
    }

    func testTheProtocolErrorsKeepTheirCodes() throws {
        let conversation = dispatcher()
        XCTAssertEqual(try reply(conversation.receive("{nope"))["error"]?["code"], .int(-32700))
        XCTAssertEqual(try reply(conversation.receive(#"{"id":1,"foo":2}"#))["error"]?["code"], .int(-32600))
        XCTAssertEqual(try reply(conversation.receive(#"{"id":1,"method":"nope"}"#))["error"]?["code"], .int(-32601))
        XCTAssertEqual(try reply(conversation.receive(#"{"id":1,"method":"tools/call","params":{}}"#))["error"]?["code"], .int(-32602))
        XCTAssertEqual(try reply(conversation.receive(#"{"id":1,"method":"tools/call","params":{"name":"nope"}}"#))["error"]?["code"], .int(-32602))
        XCTAssertEqual(try reply(conversation.receive(#"{"id":1,"method":"tools/call","params":{"name":"one","arguments":[1]}}"#))["error"]?["code"], .int(-32602))
    }

    func testAResponseOfTheClientIsIgnored() throws {
        guard case .reply(let reply) = dispatcher().receive(#"{"id":1,"result":{}}"#) else { return XCTFail() }
        XCTAssertNil(reply)
    }

    func testAToolCallIsHandedBackWithItsArgumentsAndNeverRunByTheDispatcher() throws {
        var ran = false
        let conversation = MCPDispatcher(context: .live, tools: [tool("one") { _ in ran = true; return .data([:]) }], serverVersion: "1")
        guard case .call(let id, let found, let arguments) = conversation.receive(#"{"id":7,"method":"tools/call","params":{"name":"one","arguments":{"a":1}}}"#) else {
            return XCTFail("a call was expected")
        }
        XCTAssertEqual(id, .int(7))
        XCTAssertEqual(found.name, "one")
        XCTAssertEqual(arguments["a"], .int(1))
        XCTAssertFalse(ran)
        guard case .call(_, _, let empty) = conversation.receive(#"{"id":8,"method":"tools/call","params":{"name":"one"}}"#) else { return XCTFail() }
        XCTAssertEqual(empty, [:])
    }

    func testAToolThatFailsIsAResultWithIsErrorAndNotAProtocolError() {
        let failed = MCPDispatcher.toolReply(id: .int(1), .failure(MCPToolError("No scene is open.")))
        XCTAssertEqual(failed["result"]?["isError"], .bool(true))
        XCTAssertEqual(failed["result"]?["content"]?.array?.first?["text"]?.string, "No scene is open.")
        XCTAssertNil(failed["error"])
        struct Other: LocalizedError { var errorDescription: String? { "other" } }
        XCTAssertEqual(MCPDispatcher.toolReply(id: .int(1), .failure(Other()))["result"]?["content"]?.array?.first?["text"]?.string, "other")
    }

    func testAToolThatSucceedsCarriesItsContentAndItsData() {
        let done = MCPDispatcher.toolReply(id: .int(1), .success(.data(["n": 3])))
        XCTAssertEqual(done["result"]?["structuredContent"]?["n"], .int(3))
        XCTAssertNil(done["result"]?["isError"])
    }
}

/// The socket on a real descriptor, in a folder of its own.
final class AgentSocketServerTests: XCTestCase {
    private var folder: String!

    override func setUpWithError() throws {
        // A short path: the temporary folder of a test run is longer than a socket address.
        folder = "/tmp/helix-test-\(getpid())-\(UInt32.random(in: 0..<1_000_000))"
        try FileManager.default.createDirectory(atPath: folder, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
    }

    override func tearDownWithError() throws {
        try? FileManager.default.removeItem(atPath: folder)
    }

    private var path: String { folder + "/mcp.sock" }

    /// A server that answers each line with "echo: <line>".
    private func echoServer(path: String, maxMessageBytes: Int = AgentSocket.maxMessageBytes) -> AgentSocketServer {
        let server = AgentSocketServer(path: path, maxMessageBytes: maxMessageBytes)
        server.onConnection = { connection in
            connection.onLine = { connection.send("echo: \($0)") }
            connection.onEnd = { connection.close() }
            connection.onOversize = { connection.send("too large"); connection.close() }
            connection.start()
        }
        return server
    }

    private func readLine(_ descriptor: Int32, timeout: TimeInterval = 5) -> String? {
        var bytes: [UInt8] = []
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            var poller = pollfd(fd: descriptor, events: Int16(POLLIN), revents: 0)
            guard poll(&poller, 1, 200) > 0 else { continue }
            var byte: UInt8 = 0
            let count = read(descriptor, &byte, 1)
            if count <= 0 { return bytes.isEmpty ? nil : String(decoding: bytes, as: UTF8.self) }
            if byte == UInt8(ascii: "\n") { return String(decoding: bytes, as: UTF8.self) }
            bytes.append(byte)
        }
        return nil
    }

    func testALineGoesInAndTheReplyComesBack() throws {
        let server = echoServer(path: path)
        XCTAssertEqual(server.start(), .listening)
        defer { server.stop() }
        let client = try XCTUnwrap(AgentSocket.connect(to: path))
        defer { close(client) }
        XCTAssertTrue(AgentSocket.writeAll(client, Array("hello\nworld\n".utf8)))
        XCTAssertEqual(readLine(client), "echo: hello")
        XCTAssertEqual(readLine(client), "echo: world")
    }

    func testTheSocketIsPrivateToItsOwner() throws {
        let server = echoServer(path: path)
        XCTAssertEqual(server.start(), .listening)
        defer { server.stop() }
        var info = stat()
        XCTAssertEqual(lstat(path, &info), 0)
        XCTAssertEqual(info.st_mode & 0o777, 0o600)
        XCTAssertEqual(info.st_uid, getuid())
        XCTAssertEqual(AgentSocket.occupant(of: path), .socket(owner: getuid()))
    }

    func testTheFolderOfTheDefaultPathIsMadePrivate() throws {
        let nested = folder + "/sub/mcp.sock"
        let server = echoServer(path: nested)
        XCTAssertEqual(server.start(), .listening)
        defer { server.stop() }
        var info = stat()
        XCTAssertEqual(lstat(folder + "/sub", &info), 0)
        XCTAssertEqual(info.st_mode & 0o777, 0o700)
    }

    func testAnotherServerDoesNotTakeTheSocketOfOneThatAnswers() throws {
        let first = echoServer(path: path)
        XCTAssertEqual(first.start(), .listening)
        defer { first.stop() }
        let second = echoServer(path: path)
        XCTAssertEqual(second.start(), .ownedByAnother)
        // The first still answers, and the failed one did not remove its file.
        let client = try XCTUnwrap(AgentSocket.connect(to: path))
        defer { close(client) }
        XCTAssertTrue(AgentSocket.writeAll(client, Array("still\n".utf8)))
        XCTAssertEqual(readLine(client), "echo: still")
    }

    func testASocketNobodyAnswersOnIsReplaced() throws {
        // What a Helix that crashed leaves behind: a socket file with nothing behind it.
        let stale = try XCTUnwrap(AgentSocket.makeSocket())
        var address = try XCTUnwrap(AgentSocket.address(of: path))
        let bound = withUnsafePointer(to: &address) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { Darwin.bind(stale, $0, socklen_t(MemoryLayout<sockaddr_un>.size)) }
        }
        XCTAssertEqual(bound, 0)
        close(stale)
        XCTAssertEqual(AgentSocket.occupant(of: path), .socket(owner: getuid()))
        XCTAssertNil(AgentSocket.connect(to: path))

        let server = echoServer(path: path)
        XCTAssertEqual(server.start(), .listening)
        defer { server.stop() }
        XCTAssertNotNil(AgentSocket.connect(to: path).map { close($0) })
    }

    func testAFileThatIsNotASocketIsLeftAlone() throws {
        FileManager.default.createFile(atPath: path, contents: Data("mine".utf8))
        let server = echoServer(path: path)
        guard case .failed = server.start() else { return XCTFail("a regular file must not be replaced") }
        XCTAssertEqual(try String(contentsOfFile: path, encoding: .utf8), "mine")
    }

    func testStoppingRemovesTheSocketAndNothingAnswersAfter() throws {
        let server = echoServer(path: path)
        XCTAssertEqual(server.start(), .listening)
        server.stop()
        XCTAssertEqual(AgentSocket.occupant(of: path), .nothing)
        XCTAssertNil(AgentSocket.connect(to: path))
    }

    func testAMessageLargerThanTheLimitIsRefusedAndTheConnectionClosed() throws {
        let server = echoServer(path: path, maxMessageBytes: 1024)
        XCTAssertEqual(server.start(), .listening)
        defer { server.stop() }
        let client = try XCTUnwrap(AgentSocket.connect(to: path))
        defer { close(client) }
        XCTAssertTrue(AgentSocket.writeAll(client, Array(repeating: UInt8(ascii: "a"), count: 4096)))
        XCTAssertEqual(readLine(client), "too large")
        XCTAssertNil(readLine(client, timeout: 2), "the connection is closed")
    }

    func testAClientThatHasSaidEverythingStillGetsItsReplies() throws {
        // `echo '{...}' | Helix --mcp` closes its input at once and waits for the answers.
        let server = echoServer(path: path)
        server.onConnection = { connection in
            connection.onLine = { line in DispatchQueue.global().asyncAfter(deadline: .now() + 0.3) { connection.send("late: \(line)") } }
            connection.onEnd = { DispatchQueue.global().asyncAfter(deadline: .now() + 0.6) { connection.close() } }
            connection.start()
        }
        XCTAssertEqual(server.start(), .listening)
        defer { server.stop() }
        let client = try XCTUnwrap(AgentSocket.connect(to: path))
        defer { close(client) }
        XCTAssertTrue(AgentSocket.writeAll(client, Array("one\n".utf8)))
        shutdown(client, SHUT_WR)
        XCTAssertEqual(readLine(client), "late: one")
    }

    /// A connection the app closes, and lets go of at once, still closes its descriptor: the
    /// other end reads the end of the stream. The close happens in the source's cancel handler,
    /// which runs after nothing else holds the connection.
    func testAConnectionClosedAndReleasedClosesItsDescriptor() throws {
        var pair: [Int32] = [0, 0]
        XCTAssertEqual(socketpair(AF_UNIX, SOCK_STREAM, 0, &pair), 0)
        let peer = pair[1]
        defer { Darwin.close(peer) }
        do {
            let connection = AgentConnection(descriptor: pair[0])
            connection.start()
            connection.close()
        }
        var timeout = timeval(tv_sec: 2, tv_usec: 0)
        setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
        var byte: UInt8 = 0
        let count = read(peer, &byte, 1)
        XCTAssertEqual(count, 0, "the other end should read the end of the stream, got \(count) (errno \(errno))")
    }

    func testSeveralClientsAreServedAtOnce() throws {
        let server = echoServer(path: path)
        XCTAssertEqual(server.start(), .listening)
        defer { server.stop() }
        let one = try XCTUnwrap(AgentSocket.connect(to: path))
        let two = try XCTUnwrap(AgentSocket.connect(to: path))
        defer { close(one); close(two) }
        XCTAssertTrue(AgentSocket.writeAll(one, Array("a\n".utf8)))
        XCTAssertTrue(AgentSocket.writeAll(two, Array("b\n".utf8)))
        XCTAssertEqual(readLine(two), "echo: b")
        XCTAssertEqual(readLine(one), "echo: a")
    }
}
