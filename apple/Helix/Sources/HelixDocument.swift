import CxxStdlib
import AppKit
import Combine
import Foundation
import HelixCore
import SwiftUI
import UniformTypeIdentifiers

extension UTType {
    /// A Helix scene, a `.kut` file. The file is JSON text.
    static let helixScene = UTType(exportedAs: "com.kyyzen.helix.scene")
}

/// A scene open in a window.
///
/// The scene itself lives in the engine, which holds one scene at a time and has to be driven
/// from the main thread, so a document holds the engine for as long as its window is open (see
/// `EngineHost`). A reference document, since the state it stands for is not a value it could
/// copy.
///
/// SwiftUI makes documents on a background thread, and makes one that is thrown away every time
/// a file is opened. So `init()` only notes that this is a new scene, and the engine is touched
/// by `activate()`, on the main thread, when the document is actually shown.
@MainActor
final class HelixDocument: ReferenceFileDocument {
    /// Scenes, and the 3D files Helix imports. The file of a 3D model is never written back: a
    /// scene that imports it saves as a `.kut`.
    nonisolated static var readableContentTypes: [UTType] { [.helixScene] + ImportFormats.contentTypes }
    nonisolated static var writableContentTypes: [UTType] { [.helixScene] }

    /// The name of the node the viewport shows, or nil when there is none.
    @Published private(set) var displayNodeName: String?
    @Published private(set) var pointCount: UInt64 = 0
    @Published private(set) var triangleCount: UInt64 = 0
    /// Why the viewport shows no geometry, for a person. Nil when it shows some.
    @Published private(set) var viewportMessage: String?
    /// Whether the document holds the engine, so its scene is loaded and can be drawn.
    @Published private(set) var isActive = false
    /// Whether the scene has something to play, which the timeline bar in Automatic mode follows.
    @Published var sceneAnimates = false
    /// A failure to tell the person about, in an alert.
    @Published var presentedError: String?
    /// An edit that would make a very large geometry, waiting for the person to say to go on.
    @Published var largeEditRequest: LargeEditRequest?
    /// A quiet word over the viewport when it holds a very large geometry. Nil otherwise.
    @Published var heavyNotice: String?
    /// The file being read for an import, while the sheet over the window says so.
    @Published private(set) var importProgress: ImportProgress?
    /// The question asked when another scene holds the engine: close it, and open this one?
    @Published var switchRequest: SceneSwitchRequest?

    let viewport = ViewportController()

    /// What the node editor of the window shows and does.
    let editor = NodeEditorModel()

    /// What the inspector of the window shows and does: the parameters of the selected node.
    let inspector = InspectorModel()

    /// The playhead, the range and the keys of the node in front, and the playback.
    let timeline = TimelineModel()

    /// The table of the attributes of the node in front.
    let spreadsheet = SpreadsheetModel()
    let spreadsheetSource = FacadeSpreadsheetSource()

    /// The drag in progress that is to become one step of undo, if there is one.
    var continuousEdit = ContinuousEdit()

    /// Tells when a cook brings the camera to the geometry.
    private var framing = FramingPolicy()

    /// The value a drag was taking a node to when it went past what is asked about, held until
    /// the drag ends (see `HelixDocument+Limits`).
    var heldLargeEdit: LargeEditRequest?

    /// Which read the sheet of an import stands for, so that a read that was cancelled does not
    /// import when it ends (see `HelixDocument+Import`).
    var importGeneration = 0
    func setImportProgress(_ progress: ImportProgress?) { importProgress = progress }

    /// The window of the document, which the view hands over. Weak: the window owns the view.
    weak var window: NSWindow?

    /// Holds the answer of the Save panel of the scene that is being closed to make room.
    var sceneCloser: SceneCloser?

    /// Keeps the inspector following the selection of the node editor.
    var inspectorSubscription: AnyCancellable?

    /// The undo manager of the window, which the view hands over once it has one. Weak, since
    /// the window owns it and it holds no reference to the document.
    weak var undoManager: UndoManager?

    /// The scene as a file holds it, for a save that is asked for off the main thread (see
    /// `HelixDocument+Saving`).
    nonisolated let savedScene = SceneSnapshotCache(currentRevision: { helix.core.sceneRevision() })
    var snapshotKeeper: SceneSnapshotKeeper?

    /// The file the scene is read from, or nil for a new scene. Set once, before the document is
    /// shared, so it can be read from any thread.
    private nonisolated let fileContents: Data?

    /// The copy of the opened 3D file made in a temporary folder, or nil. Set once, like
    /// `fileContents`. It is only a fallback: the import reads the file where it really is (see
    /// `finishOpening`), so that the path the scene saves stays valid and a `.gltf` finds its
    /// `.bin` and textures.
    private nonisolated let importURL: URL?

    /// Whether the 3D file this scene was opened for is still to be imported, which the view does
    /// once it knows the real URL of the file.
    private(set) var pendingOpenedImport = false

    /// A new scene, with a cube to look at, or with the 3D file that was opened. The engine is
    /// not touched until `activate`.
    nonisolated init() {
        fileContents = nil
        importURL = nil
    }

    /// A scene stored in a file. Loads it at once, so that a file that cannot be read, or a
    /// scene opened while another is open, fails here and SwiftUI shows the alert.
    nonisolated required init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else {
            throw HelixError.engine(String(localized: "The file could not be read."))
        }
        if configuration.contentType == .helixScene {
            fileContents = data
            importURL = nil
        } else {
            // A 3D file opened through the document group (Open, Dock, double click). A document
            // is given the bytes and not the path (`ReadConfiguration` has no URL), so they are
            // copied to a temporary folder, under the name of the file. That copy is not what the
            // scene imports: the window gets the real URL from SwiftUI and `finishOpening`
            // imports from it. The copy is the fallback for a file SwiftUI gives no URL for.
            fileContents = nil
            importURL = try Self.stage(data, named: configuration.file.preferredFilename ?? "model")
        }
        // Another scene holding the engine is not a failure of this file: the window opens and
        // asks whether to close the other one (see `HelixDocument+SceneSwitch`).
        try Self.onMainThread {
            do { try self.activate() } catch HelixError.busy {}
        }
    }

    private nonisolated static func stage(_ data: Data, named name: String) throws -> URL {
        let folder = FileManager.default.temporaryDirectory.appending(path: "helix-opened-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        let url = folder.appending(path: name)
        try data.write(to: url)
        return url
    }

    /// Runs on the main thread, which is where the engine lives, and waits for it.
    private nonisolated static func onMainThread(_ work: @MainActor () throws -> Void) throws {
        if Thread.isMainThread { return try MainActor.assumeIsolated(work) }
        try DispatchQueue.main.sync { try MainActor.assumeIsolated(work) }
    }

    // MARK: The engine

    /// Takes the engine and loads the scene into it. Throws `HelixError.busy` when another
    /// scene holds the engine. Does nothing when the document already holds it.
    func activate() throws {
        guard !isActive else { return }
        try EngineHost.shared.claim(for: self)
        do {
            if let fileContents {
                try loadScene(from: fileContents)
            } else if importURL != nil {
                // The import itself waits for the real URL of the file (`finishOpening`).
                try EngineHost.shared.makeEmptyScene()
                pendingOpenedImport = true
            } else {
                // A new scene is empty: the welcome screen offers what to start from.
                try EngineHost.shared.makeEmptyScene()
                Self.logUntitledScene()
                scheduleDebugImportAfterLaunch()
            }
        } catch {
            EngineHost.shared.release(self)
            throw error
        }
        isActive = true
        cookGeometry()
        prepareEditor()
        prepareTimeline()
        startKeepingSavedScene()
        if !pendingOpenedImport { Self.logLaunchDebug() }
        scheduleDebugOpenSecondScene()
        scheduleDebugFramingCheck()
        scheduleDebugNodeEditCheck()
        scheduleDebugAnimateCheck()
    }

    /// Imports the 3D file this scene was opened for, from where it really is: `url` is what
    /// SwiftUI gives the window. The `geometryImport` node stores the path it imports from and
    /// the scene saves it, so a path in a temporary folder would be lost, and a `.gltf` would not
    /// find its `.bin` and textures. Without a URL (SwiftUI should always give one for a file
    /// that was opened), the temporary copy is imported and kept, since the scene points to it.
    ///
    /// Part of opening, so it is no step of undo and does not mark the document as edited. Does
    /// nothing once done, or for a scene that was not opened from a 3D file.
    func finishOpening(fileURL url: URL?) async {
        guard pendingOpenedImport, isActive else { return }
        pendingOpenedImport = false
        let real = url.flatMap { $0.isFileURL ? $0 : nil }
        let scoped = real?.startAccessingSecurityScopedResource() ?? false
        defer { if scoped { real?.stopAccessingSecurityScopedResource() } }
        let target = real ?? importURL ?? URL(fileURLWithPath: "/")
        // The file is read off the main thread, behind a sheet with a Cancel button, so the new
        // window does not freeze for seconds on a large model. Cancelling leaves the scene as a
        // new one is, empty.
        let read = await readForImport(target)
        guard isActive else { return }
        switch read {
        case .ready:
            if let message = importNode(target) {
                presentedError = message
            } else {
                // A model that was opened is first of all to be looked at: the viewport takes the room.
                // The one place a layout is chosen for the person; the View menu has the others.
                WindowLayout.viewing.apply()
            }
        case .failed(let message):
            presentedError = message
        case .cancelled:
            break
        }
        // The copy is not needed once the real file is imported (or has failed to be).
        if real != nil, let staged = importURL { try? FileManager.default.removeItem(at: staged.deletingLastPathComponent()) }
        // The scene is a new one that holds the model, not the model's file (see `HelixDocument+Window`).
        await makeUntitledScene()
        cookGeometry()
        showAndSelectDisplayNode()
        Self.logLaunchDebug()
    }

    /// With `HELIX_DEBUG_LAUNCH=1`, says that an untitled scene opened, which `Scripts/launch-smoke.py`
    /// reads to check that a plain launch opens a window and not the Open panel.
    private static func logUntitledScene() {
        guard ProcessInfo.processInfo.environment["HELIX_DEBUG_LAUNCH"] == "1" else { return }
        FileHandle.standardError.write(Data("helix-launch: untitled scene\n".utf8)) // l10n-ok: read by launch-smoke.py
    }

    /// With `HELIX_DEBUG_LAUNCH=1` in the environment, writes the nodes of the scene that was just
    /// opened to standard error ("helix-launch: scene nodes=2 types=geometryImport,... paths=...
    /// import=..."), which `Scripts/launch-smoke.py` reads to check that a file opened as a scene,
    /// from the file itself and with its textures.
    private static func logLaunchDebug() {
        guard ProcessInfo.processInfo.environment["HELIX_DEBUG_LAUNCH"] == "1" else { return }
        let nodes = helix.core.listNodes()
        let types = nodes.map { String(engine: $0.type) }
        let paths = nodes.filter { String(engine: $0.type).hasSuffix("geometryImport") }.map {
            String(engine: helix.core.getString($0.id, "filePath", 0).value)
        }
        // From what the cook found, per material, not from the wording of the summary.
        let missing = nodes.contains { node in
            String(engine: node.type).hasSuffix("geometryImport")
                && helix.core.cookNodeReport(node.id).materials.contains { !$0.missingTextureName.isEmpty }
        }
        FileHandle.standardError.write(Data(
            ("helix-launch: scene nodes=\(types.count) types=\(types.joined(separator: ","))" // l10n-ok: read by launch-smoke.py
                + " paths=\(paths.joined(separator: "|")) missingTexture=\(missing)\n").utf8)) // l10n-ok
    }

    /// Asks for a 3D file with an open panel, and imports it into this scene.
    func chooseFileToImport() {
        let panel = NSOpenPanel()
        panel.title = String(localized: "Import")
        panel.prompt = String(localized: "Import")
        panel.message = String(localized: "Choose a 3D file to add to the scene. The file is not changed.")
        panel.allowedContentTypes = ImportFormats.contentTypes
        panel.allowsOtherFileTypes = false
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        startImport(url)
    }

    /// Imports a file that was dropped on the window, when it is a 3D file Helix reads.
    func importDroppedFile(_ url: URL) {
        guard ImportFormats.canImport(url) else {
            presentedError = String(localized: "Helix cannot import this kind of file.")
            return
        }
        startImport(url)
    }

    /// Imports a 3D file into the scene, makes it the node the viewport shows and frames it.
    /// Returns why it could not be imported, or nil. The file is only read.
    ///
    /// Once the scene is shown, the import is a step of undo. While it opens, it is not: there is
    /// nothing before it to go back to.
    @discardableResult
    func importFile(_ url: URL) -> String? {
        guard isActive else { return importNode(url) }
        let outcome = performEdit(named: String(localized: "Import")) {
            importNode(url).map { .failed($0) } ?? .done
        }
        if let error = outcome.error {
            presentedError = error
            return error
        }
        showAndSelectDisplayNode()
        return nil
    }

    /// Makes the import node and shows it, with no undo and no cook.
    func importNode(_ url: URL) -> String? {
        let result = helix.core.importFile(std.string(url.path))
        guard result.status.ok else { return String(engineMessage: result.status.error) }
        let display = helix.core.setDisplayNode(result.id)
        guard display.ok else { return String(engineMessage: display.error) }
        return nil
    }

    private func loadScene(from data: Data) throws {
        let url = FileManager.default.temporaryDirectory.appending(path: "helix-open-\(UUID().uuidString).kut")
        try data.write(to: url)
        defer { try? FileManager.default.removeItem(at: url) }
        try HelixError.check(helix.core.openScene(std.string(url.path)))
    }

    /// Starts the viewport renderer on the shared device. Returns the device, or nil after
    /// reporting why not.
    func startViewport() -> MTLDevice? {
        guard let device = EngineHost.shared.metalDevice else {
            reportViewportFailure(String(localized: "No Metal device is available."))
            return nil
        }
        // The facade borrows the device for the call and keeps its own reference.
        let status = helix.core.startViewport(Unmanaged.passUnretained(device as AnyObject).toOpaque())
        guard status.ok else {
            reportViewportFailure(String(engineMessage: status.error))
            return nil
        }
        return device
    }

    func reportViewportFailure(_ message: String) {
        viewportMessage = message
        presentedError = message
    }

    /// Cooks the display node and hands its geometry to the viewport. The camera is left where the
    /// person put it (see `FramingPolicy`): it frames only when `reframe` asks for it, or when the
    /// viewport had nothing to show and now has something, which is how a first node, or an
    /// opened scene, is brought into view.
    func cookGeometry(reframe: Bool = false) {
        let info = helix.core.updateViewportGeometry()
        let name = Self.currentDisplayNodeName()
        // Assigned only when they changed: a published property tells the window it changed
        // whenever it is written, and playback writes these at every frame.
        if displayNodeName != name { displayNodeName = name }
        if info.status.ok {
            if pointCount != info.pointCount { pointCount = info.pointCount }
            if triangleCount != info.triangleCount { triangleCount = info.triangleCount }
            if viewportMessage != nil { viewportMessage = nil }
            updateHeavyNotice(pointCount: info.pointCount)
        } else {
            if pointCount != 0 { pointCount = 0 }
            if triangleCount != 0 { triangleCount = 0 }
            updateHeavyNotice(pointCount: 0)
            let message = name == nil
                ? String(localized: "No node is displayed.")
                : String(engineMessage: info.status.error)
            if viewportMessage != message { viewportMessage = message }
        }
        let hasGeometry = info.status.ok && (info.pointCount > 0 || info.triangleCount > 0)
        if framing.shouldFrame(hasGeometry: hasGeometry, requested: reframe) {
            viewport.requestFrame()
        } else {
            viewport.redraw()
        }
    }

    private static func currentDisplayNodeName() -> String? {
        let display = helix.core.displayNode()
        guard display != 0 else { return nil }
        for node in helix.core.listNodes() where node.id == display {
            return String(engine: node.name)
        }
        return nil
    }

    func requestFrame() {
        viewport.requestFrame()
    }

    /// Whether this is a new scene nothing was done to: no file behind it, no node, and no change
    /// at all — an edit that adds no node (the frame range, the frame rate) counts, so it is
    /// checked through the engine's modified flag rather than the node count alone. Another
    /// scene that is opened takes the engine from it (see `EngineHost.claim`), and its window
    /// closes, as the empty untitled window of any document app does when a file is opened.
    var isPristineUntitled: Bool {
        isActive && fileContents == nil && importURL == nil && editor.graph.nodes.isEmpty
            && !helix.core.isSceneModified()
    }

    /// Opens the node palette. Only the node editor presents it, so the lower pane switches to
    /// the network first: from the spreadsheet, the welcome screen or the Add menu would
    /// otherwise open a palette nobody sees.
    func showNodePalette() {
        UserDefaults.standard.set(TimelineSetting.LowerPane.network.rawValue, forKey: TimelineSetting.lowerPaneKey)
        editor.openPalette()
    }

    /// Set when a scene that was opened took the engine from this empty one.
    @Published private(set) var wasReplaced = false

    func yieldEngine() {
        wasReplaced = true
        relinquish()
    }

    /// Gives the engine back, once the window is closed.
    func relinquish() {
        guard isActive else { return }
        // A playback that goes on would cook a scene the window no longer holds.
        timeline.pause()
        isActive = false
        stopKeepingSavedScene()
        // Nothing the viewport calls back into may point at the document once it is closed.
        viewport.onFailure = nil
        EngineHost.shared.release(self)
    }
}
