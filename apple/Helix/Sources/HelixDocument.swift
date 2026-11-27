import CxxStdlib
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
    nonisolated static var readableContentTypes: [UTType] { [.helixScene] }

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

    /// The file the scene is read from, or nil for a new scene. Set once, before the document is
    /// shared, so it can be read from any thread.
    private nonisolated let fileContents: Data?

    /// A new scene, with a cube to look at. The engine is not touched until `activate`.
    nonisolated init() {
        fileContents = nil
    }

    /// A scene stored in a file. Loads it at once, so that a file that cannot be read, or a
    /// scene opened while another is open, fails here and SwiftUI shows the alert.
    nonisolated required init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else {
            throw HelixError.engine(String(localized: "The file could not be read."))
        }
        fileContents = data
        try Self.onMainThread { try self.activate() }
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
            } else {
                try EngineHost.shared.makeDefaultScene()
            }
        } catch {
            EngineHost.shared.release(self)
            throw error
        }
        isActive = true
        cookGeometry()
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

    /// Cooks the display node and hands its geometry to the viewport.
    func cookGeometry() {
        let info = helix.core.updateViewportGeometry()
        displayNodeName = Self.currentDisplayNodeName()
        if info.status.ok {
            pointCount = info.pointCount
            triangleCount = info.triangleCount
            viewportMessage = nil
        } else {
            pointCount = 0
            triangleCount = 0
            viewportMessage = displayNodeName == nil
                ? String(localized: "No node is displayed.")
                : String(engine: info.status.error)
        }
        viewport.requestFrame()
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
        isActive = false
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
