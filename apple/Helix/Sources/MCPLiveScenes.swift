import AppKit
import Foundation
import HelixCore

/// A new scene and an open one, asked for by an agent.
///
/// The scene in the window is the person's work, and the engine holds one scene at a time. An
/// agent therefore cannot swap it: it asks for a window, as File > New and File > Open do, and the
/// window asks the person the question of the app (« Close the open scene, and open this one? »,
/// see `HelixDocument+SceneSwitch`), where a scene with changes asks to be saved first. The call
/// returns what the person answered, or that they have not, after a while.
@MainActor
enum MCPLiveScenes {
    /// How long a call waits for the person's answer. The question stays in the window after that,
    /// and answering it later still does what it says.
    static var answerTimeout: TimeInterval = 120

    /// How often the answer is looked for.
    private static let pollInterval: Duration = .milliseconds(100)

    static func newScene() async throws -> MCPToolOutput {
        // A scene nothing was done to is already what a new scene is.
        if let current = EngineHost.shared.activeDocument, current.isPristineUntitled {
            return result(switched: true, note: "The open scene was already a new, empty one.")
        }
        return try await switchScene { _ in
            _ = try NSDocumentController.shared.openUntitledDocumentAndDisplay(true)
        }
    }

    static func openScene(_ arguments: Arguments) async throws -> MCPToolOutput {
        let path = MCPHandlers.expanded(try arguments.requiredString("path"))
        guard FileManager.default.fileExists(atPath: path) else { throw MCPToolError("There is no file at \(path).") }
        guard (path as NSString).pathExtension.lowercased() == "kut" else {
            throw MCPToolError("open_scene opens a .kut scene. For a 3D file, use import_file.")
        }
        let url = URL(fileURLWithPath: path)
        return try await switchScene { completion in
            NSDocumentController.shared.openDocument(withContentsOf: url, display: true) { _, _, error in
                MainActor.assumeIsolated { completion(error) }
            }
        }
    }

    /// Opens a window for the new scene and waits for what comes of it: the scene is the open one
    /// now, or the person kept theirs, or has not answered.
    private static func switchScene(_ open: (@escaping @MainActor (Error?) -> Void) throws -> Void) async throws -> MCPToolOutput {
        let before = EngineHost.shared.activeDocument
        let watch = SceneSwitchWatch.shared
        watch.begin()
        var failure: Error?
        var requested = false
        do {
            try open { error in
                requested = true
                failure = error
            }
        } catch {
            throw MCPToolError("Helix could not open the scene: \(error.localizedDescription)")
        }
        // The question is for the person: the app comes to the front.
        NSApplication.shared.activate()

        let deadline = Date().addingTimeInterval(answerTimeout)
        var quietSince: Date?
        while Date() < deadline {
            if let failure { throw MCPToolError("Helix could not open the scene: \(failure.localizedDescription)") }
            if watch.outcome == .kept {
                return result(switched: false, note: "The person kept the open scene.")
            }
            if let active = EngineHost.shared.activeDocument, active !== before, active.isActive, active.switchRequest == nil {
                return result(switched: true, note: nil)
            }
            // The system opened nothing new: the scene asked for is the one that is open.
            if requested, !watch.asked, EngineHost.shared.activeDocument === before {
                quietSince = quietSince ?? Date()
                if Date().timeIntervalSince(quietSince!) > 1.5 {
                    return result(switched: false, note: "That scene is already the open one.")
                }
            }
            try? await Task.sleep(for: pollInterval)
        }
        return result(switched: false, note: "The person has not answered the question in Helix yet. It is still on their screen.")
    }

    private static func result(switched: Bool, note: String?) -> MCPToolOutput {
        var fields: [String: JSONValue] = ["switched": .bool(switched)]
        if let note { fields["note"] = .string(note) }
        if switched, EngineHost.shared.activeDocument != nil {
            fields["scene"] = MCPHandlers.sceneJSON()
        }
        return .data(.object(fields))
    }
}

/// What came of the question « Close the open scene, and open this one? », which the window of the
/// new scene asks and the person answers. The window reports it here for the call that waits.
@MainActor
final class SceneSwitchWatch {
    static let shared = SceneSwitchWatch()

    enum Outcome { case kept, switched }

    private(set) var outcome: Outcome?
    /// Whether the question was put to the person since `begin`.
    private(set) var asked = false

    func begin() {
        outcome = nil
        asked = false
    }

    func questionAsked() { asked = true }
    func answered(_ outcome: Outcome) { self.outcome = outcome }
}
