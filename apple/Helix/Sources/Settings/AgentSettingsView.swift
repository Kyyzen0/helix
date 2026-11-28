import AppKit
import SwiftUI

/// The AI Agents tab of the Settings window: whether agents may edit the open scene, where they
/// reach Helix, and the text to paste into an AI app to connect it.
struct AgentSettings: View {
    @ObservedObject private var activity = AgentLink.shared.activity
    @AppStorage(AgentLink.enabledKey) private var enabled = true
    @State private var copied: AgentSetup.Client?

    var body: some View {
        AgentsPane(
            enabled: Binding(get: { enabled }, set: { enabled = $0; AgentLink.shared.setEnabled($0) }),
            listening: activity.listening,
            socketPath: activity.socketPath,
            copied: copied,
            copy: { client in
                let text = AgentSetup.snippet(for: client, binary: AgentSetup.binaryPath)
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(text, forType: .string)
                copied = client
            }
        )
    }
}

/// The tab as a plain view of its values, which `--render-settings --tab agents` draws.
struct AgentsPane: View {
    @Binding var enabled: Bool
    var listening: AgentListening
    var socketPath: String
    var copied: AgentSetup.Client?
    var copy: (AgentSetup.Client) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 10) {
                Toggle("Let AI agents edit the open scene", isOn: $enabled)
                    .font(.headline)
                Text("An AI app on this Mac can work in the scene you have open. You see each step as it happens, and Undo (⌘Z) takes it back. Nothing leaves your Mac except what your AI app itself sends to its provider.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                Text(status)
                    .font(.callout)
                    .foregroundStyle(statusIsProblem ? Color.red : Color.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                    .accessibilityLabel(Text(status))
                Text(verbatim: socketPath)
                    .font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .textSelection(.enabled)
                    .lineLimit(2)
                    .truncationMode(.middle)
            }
            .padding(20)
            .frame(maxWidth: .infinity, alignment: .leading)
            Divider().padding(.horizontal, 20)
            VStack(alignment: .leading, spacing: 12) {
                Text("Set up your AI app").font(.headline)
                Text("Copy the setup for your AI app, paste it where the app says, and restart the app. You do this once.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                Text("Copy setup for").font(.subheadline)
                HStack(spacing: 8) {
                    ForEach(AgentSetup.Client.allCases) { client in
                        Button { copy(client) } label: { Text(verbatim: client.title) }
                    }
                }
                Text(hint)
                    .font(.callout)
                    .foregroundStyle(copied == nil ? Color.secondary : Color.primary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .padding(20)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .fixedSize(horizontal: false, vertical: true)
    }

    private var status: LocalizedStringKey {
        switch listening {
        case .off: LocalizedStringKey("Off. AI apps get a Helix of their own that nobody sees.")
        case .listening: LocalizedStringKey("Ready. AI apps connect to the Helix that is open.")
        case .ownedByAnother: LocalizedStringKey("Another Helix is already open and holds the connection.")
        case .failed: LocalizedStringKey("Helix could not open the connection for AI apps.")
        }
    }

    private var statusIsProblem: Bool {
        if case .failed = listening { return true }
        return false
    }

    private var hint: LocalizedStringKey {
        switch copied {
        case nil: LocalizedStringKey("Choose your AI app to copy its setup.")
        case .claudeCode: LocalizedStringKey("Copied. Paste it in a terminal.")
        case .claudeDesktop: LocalizedStringKey("Copied. Add it to claude_desktop_config.json, then restart Claude.")
        case .codex: LocalizedStringKey("Copied. Add it to ~/.codex/config.toml, then restart Codex.")
        case .gemini: LocalizedStringKey("Copied. Add it to ~/.gemini/settings.json, then restart Gemini.")
        case .generic: LocalizedStringKey("Copied. Paste it in the settings of your AI app.")
        }
    }
}
