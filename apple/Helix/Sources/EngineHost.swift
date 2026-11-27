import CxxStdlib
import Foundation
import HelixCore
import Metal

extension String {
    /// The text of a string of the engine, which is UTF-8.
    init(engine text: std.string) {
        self.init(decoding: text.map { UInt8(bitPattern: $0) }, as: UTF8.self)
    }
}

/// What can go wrong between the application and the engine, in words meant for a person.
enum HelixError: LocalizedError {
    case engine(String)
    case busy
    case sceneClosed

    var errorDescription: String? {
        switch self {
        case .engine(let message):
            return message
        case .busy:
            return String(localized: "Helix opens one scene at a time for now.")
        case .sceneClosed:
            return String(localized: "This scene is closed.")
        }
    }

    var recoverySuggestion: String? {
        switch self {
        case .busy:
            return String(localized: "Close the open scene, then open this one again.")
        default:
            return nil
        }
    }

    /// Turns a status of the facade into an error, or returns when it succeeded.
    static func check(_ status: helix.core.Status) throws {
        guard status.ok else { throw HelixError.engine(String(engine: status.error)) }
    }
}

/// The one engine, and the one document that owns it.
///
/// The engine is a singleton: it holds one scene, so two documents cannot be open at once.
/// A document claims the engine when it opens and gives it back when its window closes, and a
/// second claim is refused rather than shared.
@MainActor
final class EngineHost {
    static let shared = EngineHost()

    private weak var owner: AnyObject?
    private var started = false

    /// The Metal device the engine and the views share.
    let metalDevice = MTLCreateSystemDefaultDevice()

    /// Starts the engine from the folders of the application bundle, once.
    func startEngine() throws {
        guard !started else { return }

        var options = helix.core.InitOptions()
        if let resources = Bundle.main.resourceURL {
            options.nodesDirectory = std.string(resources.appending(path: "nodes").path)
            // The folder that holds `daslib`.
            options.runtimeDirectory = std.string(resources.path)
        }
        if let frameworks = Bundle.main.privateFrameworksURL {
            options.librariesDirectory = std.string(frameworks.path)
        }
        try HelixError.check(helix.core.initialize(options))
        started = true
        // A cache that cannot be made is not a reason to stop: conversions go to the temporary
        // folder then.
        try? useCacheFolder()
    }

    /// Gives the engine to a document, or refuses when another document holds it.
    func claim(for document: AnyObject) throws {
        if let owner, owner !== document { throw HelixError.busy }
        try startEngine()
        owner = document
    }

    func release(_ document: AnyObject) {
        guard owner === document else { return }
        helix.core.stopViewport()
        owner = nil
    }

    /// Replaces the scene with a new, empty one.
    func makeEmptyScene() throws {
        try HelixError.check(helix.core.newScene())
    }

    /// Where a conversion of a .blend file is kept between runs, in the caches of the user.
    func useCacheFolder() throws {
        let caches = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask).first
            ?? FileManager.default.temporaryDirectory
        let identifier = Bundle.main.bundleIdentifier ?? "Helix" // l10n-ok: a folder name
        let folder = caches.appending(path: identifier).appending(path: "blend")
        try HelixError.check(helix.core.setCacheDirectory(std.string(folder.path)))
    }

    /// Replaces the scene with a new one that shows a cube, so there is something to see.
    func makeDefaultScene() throws {
        try HelixError.check(helix.core.newScene())
        let cube = helix.core.createNode("helix::cube", "", 0, 0)
        try HelixError.check(cube.status)
        try HelixError.check(helix.core.setDisplayNode(cube.id))
    }
}
