import Foundation

/// The words of the engine, said again in the language of the app.
///
/// The engine writes its errors in English (`throwError` in a node, the status of the facade, the
/// importer, the expression compiler), and they reach the person unchanged: "The attribute needs
/// a name.", "can't locate variable 'banana'". The engine stays free of any language, so the app
/// translates on its side: a table of the messages, each with the places where a node name, a
/// parameter, a path or a number stands, matched by its words and looked up in the "Errors"
/// string table, where the English text with `%@` for those places is the key.
///
/// A message that no entry matches goes through as it came, in English, which is what the
/// engine says for a node a script defines. The messages for an agent (`Helix --mcp`) are not
/// translated: they do not come through here.
enum EngineMessages {
    static let table = "Errors" // l10n-ok: the name of the string table

    /// The translation of a text, or nil when there is none in the language the lookup serves.
    typealias Lookup = @Sendable (String) -> String?

    /// What the app looks up: the "Errors" table of the bundle, in the language of the app.
    static let bundleLookup: Lookup = { key in
        let text = Bundle.main.localizedString(forKey: key, value: nil, table: table)
        return text == key || text.isEmpty ? nil : text
    }

    // MARK: The messages

    // Every message of the engine a person can meet, as the engine writes it, with `%@` where a
    // name, a path or a number stands. The text is the key of the "Errors" table, so a new line
    // here needs its entry there (the tests check the two against each other). More particular
    // texts come before the general ones they could also match.
    static let templates: [String] = [
        // Skeleton Pose and Skin Deform.
        "The skeleton has no clip named '%@'. It has: %@.",
        "Joint %@ has a parent index that is not a joint of the skeleton.",
        "The parents of the joints form a loop, so the skeleton has no root to pose from.",
        "Connect a skeleton to the input: the second output of a Geometry Import node reading a rigged file.",
        "The input is not a skeleton: it has no joint_parent and rest_local point attributes. Use the second output of a Geometry Import node that reads a rigged file.",
        "Connect the posed skeleton to the second input: the output of a Skeleton Pose node.",
        "The second input is not a posed skeleton: it has no pose and inverse_bind point attributes. Connect the output of a Skeleton Pose node.",
        "The skeleton's pose or inverse_bind attribute does not hold a matrix for every joint.",
        "The mesh has no skin: it needs the point attributes skin_joint0 to skin_joint3 and skin_weight0 to skin_weight3. A rigged file gets them when Geometry Import reads it.",
        "Point %@ is skinned to joint %@, but the skeleton has %@ joints.",
        // Attribute nodes: Attribute Noise, Attribute Create, Sine Wave, Normal, Cell Fracture.
        "The attribute needs a name.",
        "Unknown attribute type.",
        "Unknown operation.",
        "Unknown noise type.",
        "Unknown fractal type.",
        "Unknown range.",
        "Unknown attach point.",
        "The attribute %@ can't be written as this type.",
        "The attribute %@ can't hold %@ values.",
        "The attribute %@ can't hold normals.",
        "Noise along vector needs a point attribute named %@.",
        "Noise along vector needs %@ to be a vector attribute.",
        "Wave along vector needs a point attribute named %@.",
        "Wave along vector needs %@ to be a vector attribute.",
        "The piece attribute can't be named %@.",
        // Sweep, scripts and models.
        "Sweep needs a profile shape wired into the second input.",
        "Sweep needs a profile of at least two points.",
        "This user node's source is not available.",
        "Couldn't open %@: there is no such file.",
        "Couldn't open %@ for writing",
        "Couldn't open %@.",
        "Couldn't open %@",
        // The importer.
        "%@ is a folder, not a file.",
        "%@ is larger than 2 GB, which Helix does not import.",
        "%@ is empty.",
        "There is not enough memory to import %@.",
        "A file with no extension cannot be imported. Helix reads %@.",
        "Files of type %@ cannot be imported. Helix reads %@.",
        "The file could not be imported: %@",
        "The file could not be imported.",
        "Couldn't read %@: %@",
        "Couldn't read %@.",
        "Couldn't create the conversion folder %@.",
        "Couldn't store the converted file in %@.",
        "The file holds no surface to import.",
        "The file holds no object Model I/O can read.",
        "The obj file is damaged: a line does not hold the number it should.",
        "The obj file holds no vertex.",
        "Unsupported format.",
        "The file could not be read: %@",
        "The file could not be read.",
        "The file is damaged: %@",
        "The file is damaged.",
        "Model I/O could not read the file: %@",
        "This version of macOS cannot read .%@ files.",
        "Opening .blend files needs Blender installed (blender.org).",
        "Blender was not found at %@.",
        "Blender could not be started (%@).",
        "Blender took longer than %@ minutes to convert the file and was stopped.",
        "Blender could not convert the file: %@",
        // The facade: nodes, links and the scene.
        "The engine is not initialized.",
        "There is no node %@.",
        "There is no node type '%@'.",
        "'%@' cannot name a node, use letters, digits and underscores.",
        "A node named '%@' already exists.",
        "The node %@ has no output %@.",
        "The node %@ has no input %@.",
        "The link would make the node depend on itself.",
        "Those two ports are not linked.",
        "Nothing is wired into the first input of the node.",
        "The node produced no geometry.",
        "No node is displayed.",
        "The display node produced no geometry.",
        "The folder %@ does not exist.",
        "Couldn't save the scene: %@",
        "Couldn't restore the scene: %@",
        "There is no scene file at %@.",
        "No file name was given.",
        "No file was given.",
        "No folder was given.",
        "Couldn't use %@ as the cache folder.",
        "The import node has no file path parameter.",
        "The attribute '%@' has different types on the primitives.",
        "The node has no %@ attribute named '%@'.",
        // The facade: renaming, copying and pasting nodes.
        "A name has at most %@ characters.",
        "A node that holds other nodes cannot be renamed.",
        "A node that holds other nodes cannot be copied.",
        "Only the nodes at the top of a scene can be copied.",
        "There are no nodes to copy.",
        "There are too many nodes to copy at once.",
        "The offset is not a number.",
        "The text is too large to be a copy of nodes.",
        "The text is not a copy of Helix nodes.",
        "The copy was made by another version of Helix and cannot be pasted here.",
        "The copy holds no node.",
        "The copy holds too many nodes to paste.",
        "A node that holds other nodes cannot be pasted.",
        "Only the nodes at the top of a scene can be pasted.",
        "A node of the copy has no position.",
        "A link of the copy joins nodes that are not in it.",
        "A link of the copy leaves an output that does not exist.",
        "A link of the copy enters an input that does not exist.",
        "Two links of the copy enter the same input.",
        "The links of the copy make a loop.",
        "A keyframe of the copy belongs to no node of it.",
        "A keyframe of the copy has no value.",
        "A keyframe of the copy has an unknown interpolation.",
        // The facade: parameters, expressions, ramps.
        "The node has no parameter named '%@'.",
        "Component %@ is out of range, the parameter holds %@.",
        "The parameter '%@' holds no value an expression can drive.",
        "The parameter '%@' takes one of its options, not an expression.",
        "The parameter '%@' holds keyframes, and the timeline would overwrite an expression.",
        "An expression cannot be empty. Clear the expression instead.",
        "The expression is longer than %@ characters.",
        "The parameter '%@' holds no value to reset.",
        "The parameter '%@' holds keyframes, which a reset would only be overwritten by. Clear its keyframes first.",
        "The parameter '%@' is not a ramp.",
        "A ramp has between %@ and %@ points, not %@.",
        "Point %@ of the ramp has a position or a value that is not a number.",
        "Point %@ of the ramp has an unknown interpolation '%@'.",
        "The parameter '%@' does not name an attribute.",
        // The facade: time and keyframes.
        "The parameter '%@' is not a float, and only a float can hold keyframes.",
        "A keyframe sits on a frame within a million of zero.",
        "A keyframe holds a number.",
        "The value is not a number, so it cannot be keyed.",
        "There is no keyframe on that frame.",
        "There is a keyframe on that frame already.",
        "The range ends after it starts: the last frame is above the first.",
        "A frame of the range sits within a million of zero.",
        "The frame rate is above 0 and at most 1000 frames a second.",
        // The viewport and the pictures it writes.
        "The viewport is not started.",
        "No command buffer was given.",
        "The image needs a width and a height.",
        "Nothing was drawn.",
        "Couldn't create %@.",
        "Couldn't write %@.",
        "Couldn't replace %@: %@",
        // The expression compiler (daslang), without its error code and its source lines.
        "can't locate variable '%@'",
        "syntax error, unexpected %@, expecting %@",
        "syntax error, unexpected %@",
        "no matching functions or generics: %@",
    ]

    /// The prefix a failed node puts on its message ("/bruit: ..."), which the cook of a display
    /// node gives its error: the path of the node, then the message.
    static let nodePrefixTemplate = "%@: %@"

    /// Every key of the "Errors" table.
    static var keys: [String] { templates + [nodePrefixTemplate] }

    // MARK: Translating

    private struct Entry: Sendable {
        let key: String
        let expression: NSRegularExpression
    }

    /// Each template as an expression that matches the whole message, its places capturing.
    private static let entries: [Entry] = templates.map { template in
        let parts = template.components(separatedBy: "%@").map(NSRegularExpression.escapedPattern(for:))
        // swiftlint:disable:next force_try
        let expression = try! NSRegularExpression(
            pattern: "^" + parts.joined(separator: "(.+?)") + "$", options: [.dotMatchesLineSeparators]
        )
        return Entry(key: template, expression: expression)
    }

    /// Whether the table knows a message, which the tests ask of every message the engine's
    /// sources can write.
    static func isKnown(_ message: String) -> Bool {
        let whole = NSRange(message.startIndex..., in: message)
        return entries.contains { $0.expression.firstMatch(in: message, range: whole) != nil }
    }

    // A path of the scene, a colon, and the message: "/bruit: The attribute needs a name."
    // swiftlint:disable:next force_try
    private static let nodePrefix = try! NSRegularExpression(
        pattern: #"^(/[^\s:]*): (.+)$"#, options: [.dotMatchesLineSeparators]
    )

    /// The message in the language of the lookup, or as it came when no entry matches it or the
    /// language has no translation for it.
    static func localized(_ message: String, lookup: Lookup = bundleLookup) -> String {
        // The engine names the node that failed in front of its message. The name stays, and the
        // message after it is the one translated.
        if let match = nodePrefix.firstMatch(in: message, range: NSRange(message.startIndex..., in: message)),
           let path = Range(match.range(at: 1), in: message), let rest = Range(match.range(at: 2), in: message) {
            let original = String(message[rest])
            let translated = localized(original, lookup: lookup)
            guard translated != original else { return message }
            return format(lookup(nodePrefixTemplate) ?? nodePrefixTemplate, [String(message[path]), translated])
        }
        let whole = NSRange(message.startIndex..., in: message)
        for entry in entries {
            guard let match = entry.expression.firstMatch(in: message, range: whole) else { continue }
            guard let translation = lookup(entry.key) else { return message }
            let captures = (1..<max(match.numberOfRanges, 1)).compactMap { index in
                Range(match.range(at: index), in: message).map { String(message[$0]) }
            }
            return format(translation, captures)
        }
        return message
    }

    /// Fills the places of a translation. It may put them in another order (`%2$@`).
    private static func format(_ translation: String, _ arguments: [String]) -> String {
        String(format: translation, arguments: arguments)
    }
}
