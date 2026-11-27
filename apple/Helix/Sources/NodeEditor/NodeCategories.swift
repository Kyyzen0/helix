import Foundation

/// The groups the node types are shown in where a person looks for something to add: the shelf
/// of the network pane, the Add menu and the context menu of the canvas.
///
/// A type belongs to a group by its tags and its name, which are the engine's (in English), so a
/// node a script defines, or one a later version adds, lands in "Others" until it is classified.
enum NodeCategory: String, CaseIterable, Identifiable, Sendable {
    case primitives, modify, animation, simulation, others

    var id: String { rawValue }

    var title: String {
        switch self {
        case .primitives: String(localized: "Primitives")
        case .modify: String(localized: "Modify")
        case .animation: String(localized: "Animation")
        case .simulation: String(localized: "Simulation")
        case .others: String(localized: "Others")
        }
    }

    var symbol: String {
        switch self {
        case .primitives: "cube"
        case .modify: "slider.horizontal.3"
        case .animation: "figure.walk"
        case .simulation: "atom"
        case .others: "ellipsis.circle"
        }
    }

    /// The type that imports a 3D file. It is not offered as a node of a group: choosing it
    /// asks for the file.
    static let importType = "helix::geometryImport"

    /// The group of a type, or nil for the import node.
    static func of(_ type: EditorNodeType) -> NodeCategory? {
        if type.name == importType { return nil }
        let tags = Set((type.tags + type.searchTags).map { $0.lowercased() })
        if !tags.isDisjoint(with: ["skeleton", "rig"]) { return .animation }
        if tags.contains("simulation") { return .simulation }
        if tags.contains("primitive") { return .primitives }
        let modifying: Set<String> = [
            "transform", "deform", "displace", "boolean", "combine", "modelling", "copy", "duplicate",
            "noise", "attribute", "utility", "delete", "fracture", "script", "terrain", "instance",
        ]
        if !tags.isDisjoint(with: modifying) || NodeSearch.shortName(of: type.name).hasSuffix("Noise") { return .modify } // l10n-ok: the name of a node type
        return .others
    }

    /// The types of each group, by label, with the groups that hold none left out. A type that
    /// goes by two names (an alias) shows once.
    static func groups(of types: [EditorNodeType]) -> [(category: NodeCategory, types: [EditorNodeType])] {
        var byCategory: [NodeCategory: [EditorNodeType]] = [:]
        var seen = Set<String>()
        for type in types.sorted(by: { $0.name < $1.name }) {
            guard let category = of(type) else { continue }
            guard seen.insert(category.rawValue + "/" + type.label).inserted else { continue }
            byCategory[category, default: []].append(type)
        }
        return allCases.compactMap { category in
            guard let list = byCategory[category], !list.isEmpty else { return nil }
            return (category, list.sorted { $0.label.localizedCaseInsensitiveCompare($1.label) == .orderedAscending })
        }
    }

    /// The symbol of a type in the menus.
    static func symbol(of type: EditorNodeType) -> String {
        let short = NodeSearch.shortName(of: type.name)
        if let known = symbols[short] { return known }
        return of(type)?.symbol ?? "cube"
    }

    private static let symbols: [String: String] = [
        "cube": "cube", "sphere": "circle", "torus": "circle.circle", "grid": "square.grid.3x3",
        "cylinder": "cylinder", "circle": "circle.dashed", "line": "line.diagonal", "spiral": "hurricane",
        "camera": "camera", "transform": "move.3d", "extrude": "arrow.up.to.line", "boolean": "circle.lefthalf.filled.righthalf.striped.horizontal",
        "merge": "arrow.triangle.merge", "duplicate": "plus.square.on.square", "copyToPoints": "square.on.square.dashed",
        "attributeNoise": "waveform", "positionNoise": "waveform.path", "sineWave": "water.waves", "mountain": "mountain.2",
        "delete": "trash", "script": "chevron.left.forwardslash.chevron.right", "sweep": "scribble.variable",
        "skeletonPose": "figure.stand", "skinDeform": "figure.arms.open", "simulation": "atom",
        "cellFracture": "burst", "attributeCreate": "tag", "normal": "arrow.up.right", "path": "text.append",
    ]
}
