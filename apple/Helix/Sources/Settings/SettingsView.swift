import SwiftUI

/// The Settings window (⌘,): the language, and the theme.
struct SettingsView: View {
    var body: some View {
        TabView {
            GeneralSettings()
                .tabItem { Label("General", systemImage: "gearshape") }
            AppearanceSettings()
                .tabItem { Label("Appearance", systemImage: "paintpalette") }
        }
        .frame(width: 540)
        .scenePadding()
    }
}

/// The General tab as the Settings window shows it: the choice is kept in the defaults of the app
/// as it is made, and applies the next time the app starts.
struct GeneralSettings: View {
    @State private var choice = AppLanguage.stored()

    @AppStorage(InterfaceSize.defaultsKey) private var size = InterfaceSize.default.rawValue

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            LanguagePane(
                selection: Binding(get: { choice }, set: { choice = $0; $0.store() }),
                needsRelaunch: AppLanguage.needsRelaunch(choice: choice),
                relaunch: AppRelaunch.now
            )
            Divider().padding(.horizontal, 20)
            InterfaceSizePane(selection: Binding(
                get: { InterfaceSize(rawValue: size) ?? .default },
                set: { size = $0.rawValue }
            ))
        }
    }
}

/// The interface size: how large the node editor, the timeline, the spreadsheet and the
/// inspector are drawn. It applies at once, in every window.
struct InterfaceSizePane: View {
    @Binding var selection: InterfaceSize

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Interface Size").font(.headline)
            Picker("Interface Size", selection: $selection) {
                ForEach(InterfaceSize.allCases) { size in
                    Text(size.title).tag(size)
                }
            }
            .labelsHidden()
            .pickerStyle(.segmented)
            .fixedSize()
            Text("The size of the node editor, the timeline, the spreadsheet and the inspector. It applies at once.")
                .font(.callout)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(20)
        .frame(maxWidth: .infinity, alignment: .leading)
        .fixedSize(horizontal: false, vertical: true)
    }
}

/// The language picker, the note that it applies at the next launch, and a button to launch now.
struct LanguagePane: View {
    @Binding var selection: AppLanguage
    var needsRelaunch: Bool
    var relaunch: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Language").font(.headline)
            Picker("Language", selection: $selection) {
                ForEach(AppLanguage.allCases) { language in
                    Text(language.title).tag(language)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .fixedSize()
            Text("The language will be applied the next time Helix launches.")
                .font(.callout)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            if needsRelaunch {
                Button("Relaunch Now", action: relaunch)
            }
        }
        .padding(20)
        .frame(maxWidth: .infinity, alignment: .leading)
        .fixedSize(horizontal: false, vertical: true)
    }
}

/// The Appearance tab as the Settings window shows it: the choice is the one kept in the user
/// defaults, so a click applies at once, in every window.
struct AppearanceSettings: View {
    @AppStorage(Theme.defaultsKey) private var stored = Theme.default.rawValue

    var body: some View {
        AppearancePane(selection: Binding(
            get: { Theme(rawValue: stored) ?? .default },
            set: { stored = $0.rawValue }
        ))
    }
}

/// The theme picker: a small preview of a window in each theme, and its name.
struct AppearancePane: View {
    @Binding var selection: Theme

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Theme").font(.headline)
            HStack(alignment: .top, spacing: 12) {
                ForEach(Theme.allCases) { theme in
                    ThemeTile(theme: theme, isSelected: selection == theme) { selection = theme }
                }
            }
            Text("System follows the light and dark appearance of macOS. The other themes set the appearance of the app and paint the viewport, the node editor and the timeline in their own colours.")
                .font(.callout)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .padding(20)
        .frame(maxWidth: .infinity, alignment: .leading)
        .fixedSize(horizontal: false, vertical: true)
    }
}

/// One choice of the picker.
struct ThemeTile: View {
    let theme: Theme
    let isSelected: Bool
    let choose: () -> Void
    @Environment(\.theme) private var colors

    var body: some View {
        Button(action: choose) {
            VStack(spacing: 6) {
                ThemeSwatch(theme: theme)
                    .frame(width: 84, height: 60)
                    .clipShape(RoundedRectangle(cornerRadius: 7))
                    .overlay(
                        RoundedRectangle(cornerRadius: 7)
                            .stroke(isSelected ? colors.accent : Color.primary.opacity(0.25), lineWidth: isSelected ? 3 : 1)
                    )
                Text(theme.title)
                    .font(.callout)
                    .fontWeight(isSelected ? .semibold : .regular)
            }
        }
        .buttonStyle(.plain)
        .accessibilityLabel(Text(theme.title))
        .accessibilityAddTraits(isSelected ? .isSelected : [])
    }
}

/// A miniature of a window in a theme: the viewport above, the node editor under it with two
/// cards and a link, and the timeline bar with a playhead and a key. The system theme shows
/// its light side and its dark side, split on the diagonal.
struct ThemeSwatch: View {
    let theme: Theme

    var body: some View {
        if theme == .system {
            ZStack {
                MiniWindow(palette: .dark)
                MiniWindow(palette: .light)
                    .mask(
                        GeometryReader { proxy in
                            Path { path in
                                path.move(to: .zero)
                                path.addLine(to: CGPoint(x: proxy.size.width * 0.58, y: 0))
                                path.addLine(to: CGPoint(x: proxy.size.width * 0.42, y: proxy.size.height))
                                path.addLine(to: CGPoint(x: 0, y: proxy.size.height))
                                path.closeSubpath()
                            }
                        }
                    )
            }
        } else {
            MiniWindow(palette: theme.palette(systemIsDark: false))
        }
    }
}

private struct MiniWindow: View {
    let palette: ThemePalette

    var body: some View {
        let colors = ThemeColors(palette)
        VStack(spacing: 0) {
            Rectangle().fill(Color(palette.viewportBackground))
                .overlay(
                    Circle().fill(Color(palette.viewportGeometry)).frame(width: 15, height: 15)
                )
                .frame(height: 26)
            Rectangle().fill(colors.window)
                .overlay(
                    ZStack {
                        Path { path in
                            path.move(to: CGPoint(x: 26, y: 11))
                            path.addCurve(to: CGPoint(x: 56, y: 11), control1: CGPoint(x: 38, y: 11), control2: CGPoint(x: 44, y: 11))
                        }
                        .stroke(Color(palette.nodeLink), lineWidth: 1.2)
                        card(at: CGPoint(x: 18, y: 11), colors: colors)
                        card(at: CGPoint(x: 64, y: 11), colors: colors, accented: true)
                    }
                    .frame(width: 84, height: 22, alignment: .topLeading),
                    alignment: .topLeading
                )
                .frame(height: 22)
            Rectangle().fill(Color(palette.window.mixed(with: palette.surface, by: 0.5)))
                .overlay(
                    HStack(spacing: 0) {
                        Rectangle().fill(Color(palette.cachedBar)).frame(width: 30, height: 2)
                        Rectangle().fill(Color(palette.accentOrSystem)).frame(width: 1.5)
                        Spacer(minLength: 0)
                    }
                    .padding(.leading, 6)
                    .frame(maxHeight: .infinity, alignment: .bottom)
                )
                .overlay(
                    Rectangle().fill(Color(palette.keyframe)).frame(width: 5, height: 5).rotationEffect(.degrees(45))
                        .offset(x: -14, y: -1)
                )
                .frame(height: 12)
        }
    }

    private func card(at centre: CGPoint, colors: ThemeColors, accented: Bool = false) -> some View {
        RoundedRectangle(cornerRadius: 2)
            .fill(Color(palette.surface))
            .overlay(RoundedRectangle(cornerRadius: 2).stroke(accented ? Color(palette.accentOrSystem) : Color(palette.nodeCardStroke), lineWidth: 1))
            .frame(width: 20, height: 12)
            .position(centre)
    }
}
