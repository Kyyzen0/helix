import Combine
import Foundation

/// The names AI apps give themselves in the `initialize` of the protocol, in the words a person
/// knows them by. A name nobody here knows is shown as it came.
enum AgentClients {
    /// The longest a name is shown: the name comes from another program, and is not trusted to be short.
    static let longestName = 40

    // The first that matches wins, so the more specific names are first.
    private static let known: [(needle: String, name: String)] = [
        ("claude-code", "Claude Code"), ("claude code", "Claude Code"), ("claudecode", "Claude Code"), // l10n-ok: brand names
        ("claude-ai", "Claude"), ("claude", "Claude"), // l10n-ok: brand names
        ("codex", "Codex"), // l10n-ok: brand names
        ("gemini", "Gemini"), // l10n-ok: brand names
        ("cursor", "Cursor"), // l10n-ok: brand names
        ("windsurf", "Windsurf"), // l10n-ok: brand names
        ("visual studio code", "VS Code"), ("vscode", "VS Code"), // l10n-ok: brand names
        ("zed", "Zed"), // l10n-ok: brand names
        ("cline", "Cline"), // l10n-ok: brand names
        ("goose", "Goose"), // l10n-ok: brand names
        ("glm", "GLM"), ("zhipu", "GLM"), // l10n-ok: brand names
        ("minimax", "MiniMax"), // l10n-ok: brand names
        ("opencode", "OpenCode"), // l10n-ok: brand names
    ]

    /// The name to show for what the client wrote, which is nil before it wrote anything.
    static func displayName(for raw: String?) -> String {
        let cleaned = (raw ?? "")
            .unicodeScalars.filter { !CharacterSet.controlCharacters.contains($0) }
            .reduce(into: "") { $0.unicodeScalars.append($1) }
            .trimmingCharacters(in: .whitespaces)
        guard !cleaned.isEmpty else { return String(localized: "An AI agent") }
        let lowered = cleaned.lowercased()
        // "zed" and "cline" are short enough to sit inside other words: they must be a word of their own.
        for entry in known where matches(lowered, entry.needle) { return entry.name }
        return cleaned.count > longestName ? String(cleaned.prefix(longestName)) + "…" : cleaned
    }

    private static func matches(_ name: String, _ needle: String) -> Bool {
        guard needle.count <= 5 else { return name.contains(needle) }
        let words = name.split { !$0.isLetter && !$0.isNumber }.map(String.init)
        return words.contains(needle)
    }
}

/// One agent that is connected.
struct AgentClient: Identifiable, Equatable {
    let id: UUID
    /// What the client called itself, or nil before its `initialize`.
    var rawName: String?
    let since: Date

    var displayName: String { AgentClients.displayName(for: rawName) }
}

/// One thing an agent did, for the list in the window.
struct AgentAction: Identifiable, Equatable {
    let id = UUID()
    let client: String
    let text: String
    let date: Date
    let failed: Bool
}

/// Whether the app listens for agents.
enum AgentListening: Equatable {
    case off
    case listening
    /// Another Helix has the socket: this one does not take it.
    case ownedByAnother
    case failed(String)
}

/// What the window shows of the agents: who is connected, what they did last, and whether a call
/// is running. Read by the status item of the toolbar and by the Settings tab.
@MainActor
final class AgentActivity: ObservableObject {
    /// How many actions the list keeps.
    static let keptActions = 10

    @Published private(set) var clients: [AgentClient] = []
    @Published private(set) var recent: [AgentAction] = []
    @Published private(set) var runningCalls = 0
    @Published private(set) var listening: AgentListening = .off
    /// Where the socket is, for the Settings tab to show.
    @Published private(set) var socketPath = AgentSocket.current

    var isConnected: Bool { !clients.isEmpty }
    var isRunning: Bool { runningCalls > 0 }

    /// The names of the clients, for a line of text: "Claude Code, Codex".
    var clientNames: String {
        var seen: [String] = []
        for client in clients where !seen.contains(client.displayName) { seen.append(client.displayName) }
        return seen.joined(separator: ", ")
    }

    func connected(_ client: AgentClient) {
        clients.append(client)
    }

    func renamed(_ id: UUID, to name: String?) {
        guard let index = clients.firstIndex(where: { $0.id == id }) else { return }
        clients[index].rawName = name
    }

    func disconnected(_ id: UUID) {
        clients.removeAll { $0.id == id }
    }

    func callStarted() { runningCalls += 1 }

    func callEnded(client: String, text: String, failed: Bool, at date: Date = Date()) {
        runningCalls = max(0, runningCalls - 1)
        record(client: client, text: text, failed: failed, at: date)
    }

    func record(client: String, text: String, failed: Bool, at date: Date = Date()) {
        recent.insert(AgentAction(client: client, text: text, date: date, failed: failed), at: 0)
        if recent.count > Self.keptActions { recent.removeLast(recent.count - Self.keptActions) }
    }

    func setListening(_ state: AgentListening, path: String) {
        listening = state
        socketPath = path
    }
}
