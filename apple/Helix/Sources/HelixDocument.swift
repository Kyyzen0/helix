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
final class HelixDocument: @preconcurrency ReferenceFileDocument {
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
    /// A failure to tell the person about, in an alert.
    @Published var presentedError: String?

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

    /// Keeps the inspector following the selection of the node editor.
    var inspectorSubscription: AnyCancellable?

    /// The undo manager of the window, which the view hands over once it has one. Weak, since
    /// the window owns it and it holds no reference to the document.
    weak var undoManager: UndoManager?

    /// The file the scene is read from, or nil for a new scene. Set once, before the document is
    /// shared, so it can be read from any thread.
    private nonisolated let fileContents: Data?

    /// The 3D file the scene imports, or nil. Set once, like `fileContents`.
    private nonisolated let importURL: URL?

    /// A new scene, with a cube to look at, or with the 3D file that was opened. The engine is
    /// not touched until `activate`.
    nonisolated init() {
        fileContents = nil
        importURL = ImportRequests.shared.pop()
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
            // A 3D file that reached the document itself (the document controller normally
            // takes it before). A document is given the bytes and not the path, so they are
            // copied to a folder of the caches, under the name of the file, to be imported from.
            fileContents = nil
            importURL = try Self.stage(data, named: configuration.file.preferredFilename ?? "model")
        }
        try Self.onMainThread { try self.activate() }
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
            } else if let importURL {
                try EngineHost.shared.makeEmptyScene()
                openingError = importFile(importURL)
            } else {
                try EngineHost.shared.makeDefaultScene()
            }
        } catch {
            EngineHost.shared.release(self)
            throw error
        }
        isActive = true
        cookGeometry()
        prepareEditor()
        prepareTimeline()
        if let message = openingError {
            presentedError = message
            openingError = nil
        }
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
        importFile(url)
    }

    /// Imports a file that was dropped on the window, when it is a 3D file Helix reads.
    func importDroppedFile(_ url: URL) {
        guard ImportFormats.canImport(url) else {
            presentedError = String(localized: "Helix cannot import this kind of file.")
            return
        }
        importFile(url)
    }

    /// Why the 3D file this scene was opened for could not be imported, until it is shown.
    private var openingError: String?

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
        viewport.requestFrame()
        return nil
    }

    /// Makes the import node and shows it, with no undo and no cook.
    private func importNode(_ url: URL) -> String? {
        let result = helix.core.importFile(std.string(url.path))
        guard result.status.ok else { return String(engine: result.status.error) }
        let display = helix.core.setDisplayNode(result.id)
        guard display.ok else { return String(engine: display.error) }
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
            reportViewportFailure(String(engine: status.error))
            return nil
        }
        return device
    }

    func reportViewportFailure(_ message: String) {
        viewportMessage = message
        presentedError = message
    }

    /// Cooks the display node and hands its geometry to the viewport. The camera frames the
    /// geometry afterwards, unless `reframe` is false, which is how the frame of an animation
    /// changes the picture without taking the camera from the person who placed it.
    func cookGeometry(reframe: Bool = true) {
        let info = helix.core.updateViewportGeometry()
        let name = Self.currentDisplayNodeName()
        // Assigned only when they changed: a published property tells the window it changed
        // whenever it is written, and playback writes these at every frame.
        if displayNodeName != name { displayNodeName = name }
        if info.status.ok {
            if pointCount != info.pointCount { pointCount = info.pointCount }
            if triangleCount != info.triangleCount { triangleCount = info.triangleCount }
            if viewportMessage != nil { viewportMessage = nil }
        } else {
            if pointCount != 0 { pointCount = 0 }
            if triangleCount != 0 { triangleCount = 0 }
            let message = name == nil
                ? String(localized: "No node is displayed.")
                : String(engine: info.status.error)
            if viewportMessage != message { viewportMessage = message }
        }
        if reframe { viewport.requestFrame() } else { viewport.redraw() }
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

    /// Gives the engine back, once the window is closed.
    func relinquish() {
        guard isActive else { return }
        // A playback that goes on would cook a scene the window no longer holds.
        timeline.pause()
        isActive = false
        // Nothing the viewport calls back into may point at the document once it is closed.
        viewport.onFailure = nil
        EngineHost.shared.release(self)
    }

    // MARK: Saving

    func snapshot(contentType: UTType) throws -> Data {
        guard isActive else { throw HelixError.sceneClosed }
        let url = FileManager.default.temporaryDirectory.appending(path: "helix-save-\(UUID().uuidString).kut")
        defer { try? FileManager.default.removeItem(at: url) }
        try HelixError.check(helix.core.saveScene(std.string(url.path)))
        return try Data(contentsOf: url)
    }

    nonisolated func fileWrapper(snapshot: Data, configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: snapshot)
    }
}
