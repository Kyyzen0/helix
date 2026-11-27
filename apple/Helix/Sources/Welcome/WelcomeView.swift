import AppKit
import SwiftUI

/// What the viewport of an empty scene shows: where to start. It goes away as soon as the scene
/// has a node (see `DocumentView`).
struct WelcomeView: View {
    @ObservedObject var document: HelixDocument
    @Environment(\.theme) private var theme
    @Environment(\.interfaceSize) private var size

    var body: some View {
        // The pane can be short (a small window, a large interface): the welcome gives up its
        // logo and its subtitle, then its examples, before anything would be cut off.
        ViewThatFits(in: .vertical) {
            content(logo: true, subtitle: true, examples: true, rowOfActions: false)
            content(logo: false, subtitle: false, examples: true, rowOfActions: false)
            content(logo: false, subtitle: false, examples: true, rowOfActions: true)
            content(logo: false, subtitle: false, examples: false, rowOfActions: true)
        }
        .padding(size.points(20))
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .clipped()
    }

    private func content(logo: Bool, subtitle: Bool, examples: Bool, rowOfActions: Bool) -> some View {
        VStack(spacing: size.points(rowOfActions ? 14 : 20)) {
            VStack(spacing: size.points(8)) {
                if logo {
                    Image(nsImage: NSApplication.shared.applicationIconImage)
                        .resizable()
                        .interpolation(.high)
                        .frame(width: size.points(56), height: size.points(56))
                        .accessibilityHidden(true)
                }
                Text("New Scene")
                    .uiFont(.title3, weight: .semibold)
                if subtitle {
                    Text("Import a 3D model, add a node, or start from an example.")
                        .uiFont(.body)
                        .foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            if rowOfActions {
                HStack(spacing: size.points(10)) { actions }
            } else {
                VStack(spacing: size.points(10)) { actions }
                    .frame(width: size.points(280))
            }
            if examples { exampleCards }
        }
        .controlSize(.large)
    }

    @ViewBuilder
    private var actions: some View {
        Button {
            document.chooseFileToImport()
        } label: {
            WelcomeButtonLabel(title: LocalizedStringKey("Import a 3D Model…"), symbol: "square.and.arrow.down", shortcut: "⌘I")
        }
        .buttonStyle(.borderedProminent)
        Button {
            document.editor.openPalette()
        } label: {
            WelcomeButtonLabel(title: LocalizedStringKey("Add a Node…"), symbol: "plus.square", shortcut: "⌘K")
        }
        .buttonStyle(.bordered)
        Button {
            document.chooseSceneToOpen()
        } label: {
            WelcomeButtonLabel(title: LocalizedStringKey("Open a Scene…"), symbol: "folder", shortcut: "⌘O")
        }
        .buttonStyle(.bordered)
    }

    private var exampleCards: some View {
        VStack(spacing: size.points(8)) {
            Text("Examples")
                .uiFont(.caption, weight: .semibold)
                .foregroundStyle(.secondary)
                .textCase(.uppercase)
            HStack(spacing: size.points(10)) {
                ForEach(StarterExample.allCases) { example in
                    Button { document.addStarter(example) } label: {
                        VStack(spacing: size.points(4)) {
                            Image(systemName: example.symbol)
                                .font(.system(size: size.points(22)))
                                .foregroundStyle(theme.accent)
                            Text(example.title).uiFont(.body, weight: .medium)
                            Text(example.detail)
                                .uiFont(.caption)
                                .foregroundStyle(.secondary)
                                .multilineTextAlignment(.center)
                                .lineLimit(2, reservesSpace: true)
                        }
                        .frame(width: size.points(130), height: size.points(118))
                        .padding(.horizontal, size.points(6))
                    }
                    .buttonStyle(CardButtonStyle())
                }
            }
        }
    }
}

private struct WelcomeButtonLabel: View {
    let title: LocalizedStringKey
    let symbol: String
    let shortcut: String
    @Environment(\.interfaceSize) private var size

    var body: some View {
        HStack(spacing: size.points(8)) {
            Image(systemName: symbol).frame(width: size.points(20))
            Text(title).uiFont(.body)
            Spacer(minLength: size.points(8))
            Text(shortcut).uiFont(.callout).opacity(0.6)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, size.points(3))
    }
}

/// A card to click, with a border that lights up under the pointer.
private struct CardButtonStyle: ButtonStyle {
    @State private var hovering = false
    @Environment(\.theme) private var theme

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(
                RoundedRectangle(cornerRadius: 10)
                    .fill(Color.primary.opacity(configuration.isPressed ? 0.16 : 0.07))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 10)
                    .stroke(hovering ? theme.accent : Color.primary.opacity(0.12), lineWidth: hovering ? 1.5 : 1)
            )
            .contentShape(RoundedRectangle(cornerRadius: 10))
            .onHover { hovering = $0 }
    }
}
