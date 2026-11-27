import Foundation

/// How a scene is called to a person when the app asks about it.
enum SceneNames {
    /// The name of the scene a file stands for: a `.kut` by its name as the title bar shows it, any
    /// other file (a 3D model that is being opened) by its full name, and `untitled` for a scene
    /// that is not in a file.
    static func name(for url: URL?, untitled: String) -> String {
        guard let url else { return untitled }
        return url.pathExtension.lowercased() == "kut"
            ? url.deletingPathExtension().lastPathComponent
            : url.lastPathComponent
    }
}

/// The question asked when a scene is opened while another one holds the engine.
struct SceneSwitchRequest: Identifiable, Equatable {
    let id = UUID()
    /// The scene that would be closed.
    let currentName: String
    /// The scene that would be opened.
    let newName: String

    var title: String {
        String(localized: "Close “\(currentName)” and open “\(newName)”?")
    }
}
