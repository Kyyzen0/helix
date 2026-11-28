import SwiftUI

/// The "AI" mark in the toolbar of a window while an agent is connected: who is connected, and a
/// pulse while a call runs. Clicking it opens what the agents did last, and a way to disconnect them.
struct AgentStatusItem: View {
    @ObservedObject var activity: AgentActivity
    @State private var showing = false
    /// The Helix accent, not the system one: the system accent may be red, which the app keeps
    /// for errors.
    @Environment(\.theme) private var theme

    var body: some View {
        Button { showing.toggle() } label: {
            HStack(spacing: 5) {
                Image(systemName: "sparkles")
                    .foregroundStyle(theme.accent)
                    .symbolEffect(.pulse, options: .repeating, isActive: activity.isRunning)
                Text("AI").fontWeight(.semibold)
                Text(verbatim: activity.clientNames)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
            .font(.callout)
            .padding(.horizontal, 6)
        }
        .help("AI agents are connected. Click to see what they did.")
        .accessibilityLabel(Text("AI agents"))
        .accessibilityValue(Text(verbatim: activity.clientNames))
        .popover(isPresented: $showing, arrowEdge: .bottom) {
            AgentPopover(activity: activity, disconnect: { AgentLink.shared.disconnectAll() })
        }
    }
}

/// What opens from the mark: the agents connected, the last things they did, and Disconnect.
struct AgentPopover: View {
    @ObservedObject var activity: AgentActivity
    var disconnect: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("AI agents").font(.headline)
            if activity.clients.isEmpty {
                Text("No agent is connected.").foregroundStyle(.secondary)
            } else {
                VStack(alignment: .leading, spacing: 4) {
                    ForEach(activity.clients) { client in
                        Label { Text(verbatim: client.displayName) } icon: {
                            Image(systemName: "circle.fill").font(.system(size: 6)).foregroundStyle(.green)
                        }
                    }
                }
            }
            Divider()
            Text("Last actions").font(.subheadline).foregroundStyle(.secondary)
            if activity.recent.isEmpty {
                Text("Nothing yet.").foregroundStyle(.secondary)
            } else {
                VStack(alignment: .leading, spacing: 5) {
                    ForEach(activity.recent) { action in
                        HStack(alignment: .firstTextBaseline, spacing: 6) {
                            Text(action.date, style: .time)
                                .font(.caption.monospacedDigit())
                                .foregroundStyle(.secondary)
                            Text(verbatim: action.text)
                                .foregroundStyle(action.failed ? Color.red : Color.primary)
                            Text(verbatim: action.client).font(.caption).foregroundStyle(.secondary)
                        }
                        .lineLimit(1)
                    }
                }
            }
            Text("Each change is one step of Edit ▸ Undo.")
                .font(.caption)
                .foregroundStyle(.secondary)
            HStack {
                Spacer()
                Button("Disconnect", role: .destructive, action: disconnect)
                    .disabled(activity.clients.isEmpty)
            }
        }
        .padding(16)
        .frame(width: 340, alignment: .leading)
    }
}
