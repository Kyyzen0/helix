import Foundation

// What the inspector knows about the files a node reads, and the small rules that follow from it.
// Pure values like the rest of the inspector's model, so they are tested without an engine.

/// A picture the file refers to and the import could not use.
struct MissingTexture: Equatable, Sendable {
    /// The name the file gives it, as written there (often a relative path).
    var name: String
    /// The folder it was looked for in, when it was looked for: it was not found. Nil when the
    /// picture was found and could not be read.
    var folder: String?
    /// What the engine says about a picture that was found and could not be read.
    var detail: String?
}

/// A path as the inspector shows it: the name of the file, and the folder it is in.
struct FileDisplay: Equatable {
    let name: String
    /// The folder, with the home folder written as `~`. Empty for a name with no folder.
    let folder: String

    init(path: String) {
        let text = path.trimmingCharacters(in: .whitespacesAndNewlines)
        name = (text as NSString).lastPathComponent
        let parent = (text as NSString).deletingLastPathComponent
        folder = parent.isEmpty ? "" : (parent as NSString).abbreviatingWithTildeInPath
    }
}

extension FileDisplay {
    /// A folder with the home folder written as `~`.
    static func shortened(_ folder: String) -> String {
        (folder as NSString).abbreviatingWithTildeInPath
    }

    /// The folder to open a panel in for a path: its own folder, or the nearest one above it that
    /// still exists. Nil when none does (an empty path).
    static func nearestExistingFolder(of path: String) -> String? {
        var folder = (path.trimmingCharacters(in: .whitespacesAndNewlines) as NSString).deletingLastPathComponent
        var isFolder: ObjCBool = false
        while !folder.isEmpty, folder != "/" {
            if FileManager.default.fileExists(atPath: folder, isDirectory: &isFolder), isFolder.boolValue { return folder }
            folder = (folder as NSString).deletingLastPathComponent
        }
        return nil
    }
}

extension InspectorNode {
    /// The parameters that hold the path of a file, in the order the node declares them.
    var fileParameters: [InspectorParameter] {
        layout.compactMap { item in
            guard item.kind == .parameter, let parameter = parameters[item.name],
                  parameter.style == .file, parameter.kind == .string else { return nil }
            return parameter
        }
    }

    /// The path the node reads, as it is typed: the first parameter of the file style.
    var filePath: String? {
        guard let value = fileParameters.first?.strings.first else { return nil }
        let path = value.trimmingCharacters(in: .whitespacesAndNewlines)
        return path.isEmpty ? nil : path
    }

    /// Whether a parameter is left out of the panel by a rule that the node file cannot express,
    /// because it reads the text of another parameter. The Blender to use only matters to a
    /// `.blend` file: for any other, an empty field would only ask a question nobody has.
    func isHiddenByRule(_ parameter: String) -> Bool {
        guard type == "helix::geometryImport", parameter == "blenderPath" else { return false }
        return NSString(string: filePath ?? "").pathExtension.lowercased() != "blend"
    }

    /// The parameters, with the ones a rule hides marked as hidden, which is what the tree is
    /// built from.
    var visibleParameters: [String: InspectorParameter] {
        var result = parameters
        for name in parameters.keys where isHiddenByRule(name) { result[name]?.hidden = true }
        return result
    }
}

extension InspectorParameter {
    /// The paragraphs of help for the parameter: the tooltip, then the longer documentation.
    var helpParagraphs: [String] {
        [tooltip, documentation]
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty }
    }

    /// Whether a row has an explanation to open.
    var hasHelp: Bool { !helpParagraphs.isEmpty }

    /// Whether a keyframe button makes sense: only a float follows keyframes.
    var canKeyframe: Bool { kind == .float }

    /// Whether the value has a key on the frame the scene sits on, in any component. A click on
    /// the diamond then takes the keys of that frame off, and puts them on otherwise.
    var isKeyedHere: Bool { canKeyframe && !keyed.isEmpty }

    /// Whether the row offers a reset: a parameter that holds a value, other than a path that a
    /// click would empty, or a block of code.
    var canReset: Bool {
        switch kind {
        case .other: false
        default: style != .file && style != .code
        }
    }

    /// Whether the reset is worth showing before the pointer is over the row: the value is not the
    /// default. A ramp is compared point by point by the engine and not here, so it shows on hover.
    var wantsResetShown: Bool {
        canReset && kind != .ramp && !isAtDefault
    }

    /// Whether the reset can do anything: not when the value is the default, and not while it
    /// follows keyframes, which a reset would only be overwritten by.
    var resetIsBlockedByKeyframes: Bool { !animated.isEmpty }
}
