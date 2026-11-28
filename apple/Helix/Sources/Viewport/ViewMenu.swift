import SwiftUI

/// The commands of the viewport: the camera, the frame, the wireframe and the materials, the
/// background and the layouts. The View menu and the View button of the toolbar show the same list.
///
/// The keys are on the menu items, and so work wherever the focus is: AppKit does not hand a key
/// equivalent without a command key to the menu while a text field is being edited, so an "f" typed
/// in the inspector or in the palette search is typed, not a frame (checked with a window, a field
/// editor and a menu item with key "f": the item fired with no text editing and not with it).
/// Only one of the two lists may carry the keys, or the system would find each shortcut twice.
struct ViewMenuContent: View {
    let document: HelixDocument?
    /// Whether the items carry their keys: the View menu does, the toolbar button does not.
    let shortcuts: Bool
    @AppStorage(WireframeSetting.defaultsKey) private var wireframe = WireframeSetting.auto
    @AppStorage(MaterialsSetting.defaultsKey) private var materialsVisible = true
    @AppStorage(ViewportBackground.defaultsKey) private var background = ViewportBackground.default.rawValue

    private var isActive: Bool { document?.isActive == true }

    var body: some View {
        Menu("Camera") {
            Button("Home") { document?.viewport.goHome() }
                .shortcut("0", .command, if: shortcuts)
            Divider()
            viewButton(.front)
            viewButton(.right)
            viewButton(.top)
            Divider()
            viewButton(.back)
            viewButton(.left)
            viewButton(.bottom)
        }
        .disabled(!isActive)
        Button("Frame Geometry") { document?.requestFrame() }
            .shortcut("f", [], if: shortcuts)
            .disabled(!isActive)
        Divider()
        Picker("Wireframe", selection: wireframeBinding) {
            ForEach(WireframeSetting.allCases) { setting in
                Text(setting.title).tag(setting)
            }
        }
        if shortcuts {
            Button("Cycle Wireframe") { wireframeBinding.wrappedValue = wireframe.next }
                .keyboardShortcut("w", modifiers: .shift)
        }
        Toggle("Materials", isOn: materialsBinding)
            .shortcut("m", .shift, if: shortcuts)
        Menu("Viewport Background") {
            Picker("Viewport Background", selection: $background) {
                ForEach(ViewportBackground.allCases) { choice in
                    Text(choice.title).tag(choice.rawValue)
                }
            }
            .pickerStyle(.inline)
            .labelsHidden()
        }
        Divider()
        Menu("Layout") {
            ForEach(WindowLayout.allCases) { layout in
                Button(layout.title) { layout.apply() }
            }
        }
    }

    private func viewButton(_ view: StandardView) -> some View {
        Button { document?.viewport.show(view) } label: { Text(view.title) }
            .shortcut(KeyEquivalent(view.shortcutDigit ?? " "), .command, if: shortcuts && view.shortcutDigit != nil)
    }

    private var wireframeBinding: Binding<WireframeSetting> {
        Binding(
            get: { wireframe },
            set: { setting in
                wireframe = setting
                document?.viewport.redraw()
            }
        )
    }

    private var materialsBinding: Binding<Bool> {
        Binding(
            get: { materialsVisible },
            set: { visible in
                materialsVisible = visible
                document?.viewport.redraw()
            }
        )
    }
}

extension WindowLayout {
    var title: LocalizedStringKey {
        switch self {
        case .viewing: LocalizedStringKey("Viewing")
        case .building: LocalizedStringKey("Building")
        case .animating: LocalizedStringKey("Animating")
        }
    }
}

private extension View {
    /// Gives the view a keyboard shortcut when asked to, and leaves it alone otherwise.
    @ViewBuilder
    func shortcut(_ key: KeyEquivalent, _ modifiers: EventModifiers, if enabled: Bool) -> some View {
        if enabled {
            keyboardShortcut(key, modifiers: modifiers)
        } else {
            self
        }
    }
}
