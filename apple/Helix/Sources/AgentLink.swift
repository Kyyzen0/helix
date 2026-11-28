import AppKit
import CxxStdlib
import Foundation

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

        init(connection: AgentConnection) {
            id = connection.id
            self.connection = connection
        }
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
        conversation.dispatcher.onInitialize = { [weak self] name in
            guard let self else { return }
            activity.renamed(conversation.id, to: name)
            Self.log("client connected: \(AgentClients.displayName(for: name))")
        }
        let id = conversation.id
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
            let outcome: Result<MCPToolOutput, Error>
            do {
                outcome = .success(try await MCPLive.run(tool, arguments: arguments))
            } catch {
                outcome = .failure(error)
            }
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
}
