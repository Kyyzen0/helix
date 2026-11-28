import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// The window of a document: the viewport over the node editor, the inspector on the right, the
/// timeline along the bottom, and a toolbar.
struct DocumentView: View {
    @ObservedObject var document: HelixDocument
    /// Where the document's file is, nil for a scene that was never saved.
    var fileURL: URL?
    @Environment(\.dismiss) private var dismiss
    @Environment(\.undoManager) private var undoManager
    @Environment(\.interfaceSize) private var size
    /// Whether the inspector column is shown. Kept across launches, and shared with the menu. It
    /// is shown until the person hides it.
    @AppStorage(InspectorSetting.defaultsKey) private var inspectorShown = true
    /// Whether the timeline bar is shown, and what the bottom pane holds. Kept across launches,
    /// and shared with the menu.
    @AppStorage(TimelineSetting.shownKey) private var timelineShown = true
    @AppStorage(TimelineSetting.lowerPaneKey) private var lowerPane = TimelineSetting.LowerPane.network.rawValue

    /// The smallest a window gets, and the width of the inspector column.
    static let minimumSize = WindowSize.minimum

    var body: some View {
        VStack(spacing: 0) {
            ViewportSplit(
                top: ViewportPane(document: document),
                bottom: lowerPaneView
            )
            if timelineShown, document.isActive { TimelineBar(model: document.timeline) }
        }
        .frame(minWidth: Self.minimumSize.width, minHeight: Self.minimumSize.height)
        .inspector(isPresented: $inspectorShown) {
            InspectorView(model: document.inspector)
                .inspectorColumnWidth(min: 300, ideal: 340, max: 520)
        }
        // The window's undo manager takes the steps of the node editor, and marks the document
        // as edited when it gets one.
        .onAppear {
            document.undoManager = undoManager
            document.spreadsheet.isShown = lowerPane == TimelineSetting.LowerPane.spreadsheet.rawValue
        }
        .onChange(of: lowerPane) { _, pane in
            document.spreadsheet.isShown = pane == TimelineSetting.LowerPane.spreadsheet.rawValue
        }
        .onChange(of: undoManager) { _, manager in document.undoManager = manager }
        // An empty untitled scene gives way to the scene that was opened: its window closes.
        .onChange(of: document.wasReplaced) { _, replaced in if replaced { dismiss() } }
        .task {
            if activate() { await document.finishOpening(fileURL: fileURL) }
            HelixDocument.logWindowSize()
        }
        .background(WindowReader(document: document))
        // The sheet of an import: the file is being read, and Cancel leaves the scene as it was.
        .sheet(isPresented: Binding(
            get: { document.importProgress != nil },
            set: { if !$0 { document.cancelImport() } }
        )) {
            ImportProgressSheet(fileName: document.importProgress?.fileName ?? "") { document.cancelImport() }
        }
        .onChange(of: document.switchRequest) { _, request in
            // `Scripts/launch-smoke.py` answers the question for a run with no person.
            guard request != nil, let answer = HelixDocument.debugSwitchAnswer else { return }
            Task { @MainActor in
                if answer == "close" { answerSwitch(confirm: true) } else { answerSwitch(confirm: false) }
            }
        }
        // A 3D file dropped on the window is imported into the scene.
        .onDrop(of: [.fileURL], isTargeted: nil) { providers in
            guard document.isActive else { return false }
            guard let provider = providers.first(where: { $0.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) }) else {
                return false
            }
            _ = provider.loadObject(ofClass: URL.self) { url, _ in
                guard let url else { return }
                Task { @MainActor in document.importDroppedFile(url) }
            }
            return true
        }
        .toolbar { toolbar }
        .alert(
            prompt?.title ?? Text(verbatim: ""),
            isPresented: Binding(get: { prompt != nil }, set: { if !$0 { dismissError() } }),
            presenting: prompt
        ) { prompt in
            promptButtons(prompt)
        } message: { prompt in
            Text(verbatim: prompt.message)
        }
    }

    // MARK: Toolbar

    @ToolbarContentBuilder
    private var toolbar: some ToolbarContent {
        // The size of the geometry, as a quiet label that the system moves into the overflow menu
        // before it clips it.
        ToolbarItem(placement: .status) {
            DocumentStats(document: document)
        }
        ToolbarItemGroup(placement: .primaryAction) {
            AddMenu(document: document)
            Menu {
                ViewMenuContent(document: document, shortcuts: false)
            } label: {
                Label("View", systemImage: "cube")
            }
            .help("Camera, wireframe, materials, background and layout")
            Button {
                document.requestFrame()
            } label: {
                Label("Frame Geometry", systemImage: "viewfinder")
            }
            .disabled(!document.isActive)
            .help("Frame the geometry (F)")
            Button {
                inspectorShown.toggle()
            } label: {
                Label("Inspector", systemImage: "sidebar.trailing")
            }
            .help("Show or hide the inspector (⌥⌘I)")
        }
    }
}

extension DocumentView {
    /// The bottom pane: the node editor or the spreadsheet, with the switch between them.
    private var lowerPaneView: some View {
        VStack(spacing: 0) {
            LowerPaneHeader(document: document, lowerPane: $lowerPane)
            Divider()
            if lowerPane == TimelineSetting.LowerPane.spreadsheet.rawValue {
                SpreadsheetView(model: document.spreadsheet)
            } else {
                NodeEditorView(model: document.editor)
            }
        }
    }

    /// A new scene takes the engine when it is shown. When another scene holds it, there is
    /// nowhere to put this one until that one is closed: ask whether to close it. Returns whether
    /// this scene holds the engine.
    private func activate() -> Bool {
        do {
            try document.activate()
            return true
        } catch HelixError.busy {
            document.askToSwitch(fileURL: fileURL)
            return false
        } catch {
            document.presentedError = error.localizedDescription
            return false
        }
    }

    /// What the window asks, in the order of what matters most: to close the other scene, to go
    /// on with a very large geometry, then a failure.
    private var prompt: DocumentPrompt? {
        if let request = document.switchRequest { return .switchScene(request) }
        if let request = document.largeEditRequest { return .largeEdit(request) }
        if let error = document.presentedError { return .error(error) }
        return nil
    }

    private func dismissError() {
        // The buttons of the other questions answer them; only a failure is cleared by being seen.
        if case .error = prompt { document.presentedError = nil }
    }

    @ViewBuilder
    private func promptButtons(_ prompt: DocumentPrompt) -> some View {
        switch prompt {
        case .switchScene:
            Button("Close and Open") { answerSwitch(confirm: true) }
            Button("Cancel", role: .cancel) { answerSwitch(confirm: false) }
        case .largeEdit:
            Button("Continue") { document.confirmLargeEdit() }
            Button("Cancel", role: .cancel) { document.cancelLargeEdit() }
        case .error:
            Button("OK", role: .cancel) { document.presentedError = nil }
        }
    }

    /// Closes the open scene and opens this one, or leaves things as they are and closes this
    /// window.
    private func answerSwitch(confirm: Bool) {
        guard document.switchRequest != nil else { return }
        guard confirm else {
            document.cancelSwitch()
            dismiss()
            return
        }
        document.switchRequest = nil
        let url = fileURL
        Task { @MainActor in
            if await !document.confirmSwitch(fileURL: url) { dismiss() }
        }
    }
}

/// What the window of a document asks the person, one question at a time.
private enum DocumentPrompt {
    case switchScene(SceneSwitchRequest)
    case largeEdit(LargeEditRequest)
    case error(String)

    var title: Text {
        switch self {
        case .switchScene(let request): Text(verbatim: request.title)
        case .largeEdit: Text("Very large geometry")
        case .error: Text("Helix could not do that")
        }
    }

    var message: String {
        switch self {
        case .switchScene: String(localized: "Helix works on one scene at a time.")
        case .largeEdit(let request): request.message
        case .error(let message): message
        }
    }
}

/// The sheet over a window while a file is read for an import.
struct ImportProgressSheet: View {
    let fileName: String
    let onCancel: () -> Void
    @Environment(\.interfaceSize) private var size

    var body: some View {
        VStack(spacing: size.points(14)) {
            Text("Importing \(fileName)…")
                .uiFont(.headline)
                .lineLimit(2)
                .multilineTextAlignment(.center)
            ProgressView()
                .progressViewStyle(.linear)
            Button("Cancel", role: .cancel, action: onCancel)
                .keyboardShortcut(.cancelAction)
        }
        .padding(size.points(20))
        .frame(width: size.points(340))
        .accessibilityElement(children: .contain)
    }
}

/// The row over the bottom pane: the switch between the node editor and the spreadsheet, then
/// what goes with the one that is shown: the shelf of nodes to add, or what the spreadsheet follows.
struct LowerPaneHeader: View {
    @ObservedObject var document: HelixDocument
    @Binding var lowerPane: String
    @Environment(\.interfaceSize) private var size

    private var isSpreadsheet: Bool { lowerPane == TimelineSetting.LowerPane.spreadsheet.rawValue }

    var body: some View {
        HStack(spacing: size.points(10)) {
            Picker("View", selection: $lowerPane) {
                Text("Network").tag(TimelineSetting.LowerPane.network.rawValue)
                Text("Spreadsheet").tag(TimelineSetting.LowerPane.spreadsheet.rawValue)
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .fixedSize()
            if isSpreadsheet {
                SpreadsheetHeader(model: document.spreadsheet)
            } else {
                NodeShelf(model: document.editor) { document.chooseFileToImport() }
                    .disabled(!document.isActive)
                Spacer(minLength: 0)
            }
        }
        .padding(.horizontal, size.points(10))
        .padding(.vertical, size.points(6))
    }
}

/// The geometry the viewport shows, in as few words as fit: "1,03 M points · 1,95 M triangles".
struct DocumentStats: View {
    @ObservedObject var document: HelixDocument

    var body: some View {
        Text("\(CountFormat.points(document.pointCount)) · \(CountFormat.triangles(document.triangleCount))")
            .monospacedDigit()
            .foregroundStyle(.secondary)
            .lineLimit(1)
            .fixedSize()
            .padding(.horizontal, 8)
            .help(Text("\(document.displayNodeName ?? String(localized: "No display node")): \(Int(document.pointCount).formatted()) points, \(Int(document.triangleCount).formatted()) triangles"))
            .accessibilityLabel(Text("Geometry size"))
    }
}

/// The Add button of the toolbar: a menu of the groups of nodes, the import and the search.
struct AddMenu: View {
    @ObservedObject var document: HelixDocument

    var body: some View {
        Menu {
            AddNodeSubmenus(types: document.editor.paletteTypes) { document.editor.addNode($0) }
            Divider()
            Button { document.chooseFileToImport() } label: {
                Label("Import a 3D Model…", systemImage: "square.and.arrow.down")
            }
            Button { document.showNodePalette() } label: {
                Label("Add Node…", systemImage: "magnifyingglass")
            }
        } label: {
            Label("Add", systemImage: "plus")
                .labelStyle(.titleAndIcon)
        }
        .disabled(!document.isActive)
        .help("Add a node or import a 3D model")
    }
}

/// The viewport, with the welcome screen over it while the scene holds no node, and the reason
/// there is nothing to see when there is one.
struct ViewportPane: View {
    @ObservedObject var document: HelixDocument
    @ObservedObject var editor: NodeEditorModel
    @Environment(\.theme) private var theme
    @Environment(\.viewportStill) private var still
    @AppStorage(NavigationLegendSetting.hiddenKey) private var legendHidden = false
    @AppStorage(ViewportBackground.defaultsKey) private var background = ViewportBackground.default.rawValue

    /// What the overlays are drawn for: light text on a dark background, and the other way round.
    /// It follows the luminance of the background the viewport really has, so a light background
    /// gets dark text whatever the theme of the window is.
    private var overlayScheme: ColorScheme {
        (ViewportBackground(rawValue: background) ?? .default).colors(for: theme.palette).isDark ? .dark : .light
    }

    init(document: HelixDocument) {
        self.document = document
        editor = document.editor
    }

    private var isEmpty: Bool { document.isActive && editor.graph.nodes.isEmpty }

    var body: some View {
        ZStack {
            // The viewport is made once the document holds the engine, since it draws what the
            // engine holds.
            if let still {
                StillViewport(still: still)
            } else if document.isActive {
                ViewportView(document: document)
            }
            if isEmpty {
                WelcomeView(document: document)
                    .background(theme.window)
            } else if let message = document.viewportMessage {
                Text(message)
                    .font(.title3)
                    .foregroundStyle(.secondary)
                    .padding()
                    .allowsHitTesting(false)
                    .environment(\.colorScheme, overlayScheme)
            }
        }
        // How to move around, at the bottom left, until the person hides it.
        .overlay(alignment: .bottomLeading) {
            if document.isActive, !isEmpty, !legendHidden {
                NavigationLegend { legendHidden = true }
                    .environment(\.colorScheme, overlayScheme)
                    .padding(10)
            }
        }
        // The view cube and the home button, at the top right.
        .overlay(alignment: .topTrailing) {
            if document.isActive, !isEmpty {
                ViewCubeOverlay(controller: document.viewport)
                    .environment(\.colorScheme, overlayScheme)
                    .padding(10)
            }
        }
        // The size of a cell of the grid, at the bottom right.
        .overlay(alignment: .bottomTrailing) {
            if document.isActive, !isEmpty {
                GridLabel(controller: document.viewport)
                    .environment(\.colorScheme, overlayScheme)
                    .padding(10)
            }
        }
        // A very large geometry is said once, quietly, without a button.
        .overlay(alignment: .top) {
            if let notice = document.heavyNotice {
                Label(notice, systemImage: "info.circle")
                    .uiFont(.caption)
                    .padding(.horizontal, 10)
                    .padding(.vertical, 5)
                    .background(Capsule().fill(.regularMaterial))
                    .overlay(Capsule().stroke(Color.primary.opacity(0.12)))
                    .padding(.top, 8)
                    .allowsHitTesting(false)
                    .environment(\.colorScheme, overlayScheme)
                    .accessibilityAddTraits(.updatesFrequently)
            }
        }
    }
}

/// A top and a bottom with a divider the person drags. The top takes 60 percent at first, and the
/// share is kept across launches. Neither part gets smaller than what it needs to be usable.
struct ViewportSplit<Top: View, Bottom: View>: View {
    let top: Top
    let bottom: Bottom
    @AppStorage(WindowLayout.fractionKey) private var fraction = 0.6
    @State private var dragStart: Double?

    static var minimumTop: CGFloat { 220 }
    static var minimumBottom: CGFloat { 190 }
    private static var handle: CGFloat { 9 }

    var body: some View {
        GeometryReader { proxy in
            let total = proxy.size.height
            let topHeight = Self.topHeight(fraction: fraction, total: total)
            VStack(spacing: 0) {
                top.frame(height: topHeight)
                handle(total: total, topHeight: topHeight)
                bottom.frame(maxHeight: .infinity)
            }
        }
    }

    /// The height of the top for a share of the room, kept between what each part needs.
    static func topHeight(fraction: Double, total: CGFloat) -> CGFloat {
        let most = max(total - minimumBottom - handle, minimumTop)
        return min(max(total * CGFloat(fraction), minimumTop), most)
    }

    private func handle(total: CGFloat, topHeight: CGFloat) -> some View {
        Rectangle()
            .fill(Color.clear)
            .frame(height: Self.handle)
            .overlay(Divider())
            .contentShape(Rectangle())
            .pointerStyle(.rowResize)
            .gesture(
                DragGesture(minimumDistance: 1, coordinateSpace: .global)
                    .onChanged { drag in
                        let start = dragStart ?? fraction
                        dragStart = start
                        guard total > 0 else { return }
                        let wanted = Self.topHeight(fraction: start + Double(drag.translation.height / total), total: total)
                        fraction = Double(wanted / total)
                    }
                    .onEnded { _ in dragStart = nil }
            )
            .accessibilityLabel(Text("Resize the viewport"))
    }
}
