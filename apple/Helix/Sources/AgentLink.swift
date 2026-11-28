import AppKit
import CxxStdlib
import Foundation
import os

/// The live link between the app and the AI agents on this Mac.
///
/// The app listens on a Unix socket (see `AgentSocket`), and `Helix --mcp` in an agent's
/// configuration relays to it. Each connection is a conversation in the protocol of
/// `MCPDispatcher`; the tools act on the scene open in the window (see `MCPLive`), one call at a
/// time, on the main actor, where the engine lives.
///
/// The socket is the person's own: it is private to their user, and a connection from any other
/// user is refused. Nothing here reaches the network.
@MainActor
final class AgentLink {
    static let shared = AgentLink()

    /// The setting of the Settings tab: whether agents may edit the open scene. On until the
    /// person turns it off.
    static let enabledKey = "agentsEnabled"

    /// What the window shows.
    let activity = AgentActivity()

    /// How many steps of undo the agents made, which the launch smoke test reads.
    private(set) var registeredSteps = 0
    func countStep() { registeredSteps += 1 }

    private var server: AgentSocketServer?
    private var conversations: [UUID: Conversation] = [:]
    private var jobs: [(Conversation, String)] = []
    private var draining = false
    private var debugUndoScheduled = false
    private var debugDisconnectScheduled = false
    private var signalSources: [DispatchSourceSignal] = []

    /// One connected agent.
    @MainActor private final class Conversation {
        let id: UUID
        let connection: AgentConnection
        let dispatcher = MCPDispatcher.live()
        /// Lines received and not yet answered.
        var pending = 0
        /// The agent has said it will send no more.
        var inputEnded = false
        var closed = false
        /// The call that is running for this agent. Closing the conversation cancels it: an agent
        /// that was disconnected (the toggle of the Settings, Disconnect in the popover) must not
        /// find its call finishing in the scene after the person withdrew access.
        var operation: Task<MCPToolOutput, Error>?

        /// How many conversations are alive, which `Scripts/live-smoke.py` reads to prove that a
        /// closed one is freed (a closure that holds its own conversation would keep it for good).
        private nonisolated static let alive = OSAllocatedUnfairLock(initialState: 0)
        static var aliveCount: Int { alive.withLock { $0 } }

        init(connection: AgentConnection) {
            id = connection.id
            self.connection = connection
            Self.alive.withLock { $0 += 1 }
        }

        deinit { Self.alive.withLock { $0 -= 1 } }
    }

    static var isEnabled: Bool {
        UserDefaults.standard.object(forKey: enabledKey) as? Bool ?? true
    }

    // MARK: Starting and stopping

    /// Starts listening when the setting allows it. Safe to call again.
    func start() {
        scheduleDebugUndo()
        removeSocketOnSignals()
        guard server == nil, Self.isEnabled else { return }
        let path = AgentSocket.current
        let server = AgentSocketServer(path: path)
        server.onConnection = { connection in
            // The queue of the connection hands the main actor each event in order.
            DispatchQueue.main.async { MainActor.assumeIsolated { AgentLink.shared.accept(connection) } }
        }
        switch server.start() {
        case .listening:
            self.server = server
            activity.setListening(.listening, path: path)
            Self.log("listening at \(path)")
        case .ownedByAnother:
            activity.setListening(.ownedByAnother, path: path)
            Self.log("another Helix answers at \(path): leaving it alone")
        case .failed(let message):
            activity.setListening(.failed(message), path: path)
            Self.log("not listening: \(message)")
        }
    }

    /// Stops listening, drops the agents and removes the socket. The relay of an agent that
    /// connects after this finds nothing and falls back to the headless server.
    func stop() {
        disconnectAll()
        server?.stop()
        server = nil
        activity.setListening(.off, path: activity.socketPath)
    }

    /// A Helix that is stopped from outside (kill, a terminal's interrupt) takes its socket away
    /// as it goes, as quitting does; one that crashes leaves it, which the next Helix replaces.
    private func removeSocketOnSignals() {
        guard signalSources.isEmpty else { return }
        for number in [SIGTERM, SIGINT, SIGHUP] {
            signal(number, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: number, queue: .main)
            source.setEventHandler {
                MainActor.assumeIsolated { AgentLink.shared.stop() }
                exit(0)
            }
            source.resume()
            signalSources.append(source)
        }
    }

    func setEnabled(_ enabled: Bool) {
        UserDefaults.standard.set(enabled, forKey: Self.enabledKey)
        if enabled { start() } else { stop() }
    }

    /// Closes every connection. The agents' relays end, and their apps may connect again.
    func disconnectAll() {
        for conversation in conversations.values { close(conversation) }
    }

    // MARK: Connections

    private func accept(_ connection: AgentConnection) {
        let conversation = Conversation(connection: connection)
        conversations[conversation.id] = conversation
        activity.connected(AgentClient(id: conversation.id, rawName: nil, since: Date()))
        // The dispatcher belongs to the conversation: a closure of it that held the conversation
        // would be a cycle, and a closed conversation would never be freed. So the closures take
        // the identifier, and nothing else of it.
        let id = conversation.id
        conversation.dispatcher.onInitialize = { [weak self] name in
            guard let self else { return }
            activity.renamed(id, to: name)
            Self.log("client connected: \(AgentClients.displayName(for: name))")
        }
        connection.onLine = { line in
            DispatchQueue.main.async { MainActor.assumeIsolated { AgentLink.shared.conversations[id].map { AgentLink.shared.receive(line, from: $0) } } }
        }
        connection.onEnd = {
            DispatchQueue.main.async { MainActor.assumeIsolated { AgentLink.shared.conversations[id].map { AgentLink.shared.inputEnded($0) } } }
        }
        connection.onOversize = {
            DispatchQueue.main.async { MainActor.assumeIsolated { AgentLink.shared.conversations[id].map { AgentLink.shared.tooLarge($0) } } }
        }
        connection.start()
    }

    private func receive(_ line: String, from conversation: Conversation) {
        guard !conversation.closed else { return }
        conversation.pending += 1
        jobs.append((conversation, line))
        if !draining {
            draining = true
            Task { await drain() }
        }
    }

    /// Answers the lines in the order they came, one at a time: a tool changes the scene, and
    /// two at once would be two changes in one step of undo.
    private func drain() async {
        while !jobs.isEmpty {
            let (conversation, line) = jobs.removeFirst()
            await process(line, from: conversation)
            conversation.pending -= 1
            if conversation.inputEnded, conversation.pending == 0 { close(conversation) }
        }
        draining = false
    }

    private func process(_ line: String, from conversation: Conversation) async {
        guard !conversation.closed else { return }
        switch conversation.dispatcher.receive(line) {
        case .reply(let reply):
            if let reply { conversation.connection.send(reply.serialized()) }
        case .call(let id, let tool, let arguments):
            let client = AgentClients.displayName(for: conversation.dispatcher.clientName)
            activity.callStarted()
            // The call runs as a task of its own so that closing the conversation can cancel it.
            let call = Task { try await MCPLive.run(tool, arguments: arguments) }
            conversation.operation = call
            let outcome = await call.result
            conversation.operation = nil
            var failed = false
            if case .failure = outcome { failed = true }
            activity.callEnded(client: client, text: MCPLive.activityText(tool.name), failed: failed)
            if !conversation.closed {
                conversation.connection.send(MCPDispatcher.toolReply(id: id, outcome).serialized())
            }
        }
    }

    private func inputEnded(_ conversation: Conversation) {
        conversation.inputEnded = true
        if conversation.pending == 0 { close(conversation) }
    }

    private func tooLarge(_ conversation: Conversation) {
        let reply = MCPDispatcher.failure(id: .null, code: -32600, message: "The message is larger than Helix takes.")
        conversation.connection.send(reply.serialized())
        close(conversation)
    }

    private func close(_ conversation: Conversation) {
        guard !conversation.closed else { return }
        conversation.closed = true
        conversation.operation?.cancel()
        conversation.connection.close()
        conversations[conversation.id] = nil
        activity.disconnected(conversation.id)
    }

    /// A line for the log of the app: standard error, where the debug runs read it.
    static func log(_ message: String) {
        FileHandle.standardError.write(Data("Helix MCP: \(message)\n".utf8))
    }

    // MARK: Debug

    /// With `HELIX_DEBUG_LIVE_UNDO_FILE=<path>`, undoes one step of the open scene each time that
    /// file appears (and removes it), and says what is left. `Scripts/live-smoke.py` uses it to
    /// check that a call of an agent is one step of Edit > Undo, as a person's ⌘Z would take it back.
    func scheduleDebugUndo() {
        scheduleDebugDisconnect()

        guard !debugUndoScheduled, let path = ProcessInfo.processInfo.environment["HELIX_DEBUG_LIVE_UNDO_FILE"], !path.isEmpty else { return }
        debugUndoScheduled = true
        Task { @MainActor in
            while true {
                try? await Task.sleep(for: .milliseconds(200))
                guard FileManager.default.fileExists(atPath: path) else { continue }
                try? FileManager.default.removeItem(atPath: path)
                guard let document = EngineHost.shared.activeDocument, let manager = document.undoManager else {
                    HelixDocument.debugLog("live-undo none") // l10n-ok: read by live-smoke.py
                    continue
                }
                let name = manager.undoActionName
                let could = manager.canUndo
                if could { manager.undo() }
                let nodes = MCPHandlers.nodeList().map { String(engine: $0.path) }.joined(separator: ",")
                HelixDocument.debugLog("live-undo undone=\(could) name=\(name) nodes=\(nodes) can-undo-more=\(manager.canUndo)") // l10n-ok: read by live-smoke.py
            }
        }
    }

    /// With `HELIX_DEBUG_LIVE_DISCONNECT_FILE=<path>`, disconnects every agent (what Disconnect in
    /// the popover does) each time that file appears, and says how many conversations are still
    /// alive a moment after. With `HELIX_DEBUG_LIVE_COUNT_FILE=<path>`, only says how many are.
    /// `Scripts/live-smoke.py` uses them to check that a call that was waiting is cancelled, and
    /// that a closed conversation is freed.
    private func scheduleDebugDisconnect() {
        let environment = ProcessInfo.processInfo.environment
        let disconnectPath = environment["HELIX_DEBUG_LIVE_DISCONNECT_FILE"] ?? ""
        let countPath = environment["HELIX_DEBUG_LIVE_COUNT_FILE"] ?? ""
        guard !debugDisconnectScheduled, !disconnectPath.isEmpty || !countPath.isEmpty else { return }
        debugDisconnectScheduled = true
        Task { @MainActor in
            while true {
                try? await Task.sleep(for: .milliseconds(200))
                if !disconnectPath.isEmpty, FileManager.default.fileExists(atPath: disconnectPath) {
                    try? FileManager.default.removeItem(atPath: disconnectPath)
                    disconnectAll()
                    HelixDocument.debugLog("live-disconnect done") // l10n-ok: read by live-smoke.py
                }
                if !countPath.isEmpty, FileManager.default.fileExists(atPath: countPath) {
                    try? FileManager.default.removeItem(atPath: countPath)
                    HelixDocument.debugLog("live-conversations alive=\(Conversation.aliveCount)") // l10n-ok: read by live-smoke.py
                }
            }
        }
    }
}
