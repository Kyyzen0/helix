import CxxStdlib
import Foundation
import HelixCore

extension String {
    /// The text of a long string of the engine, such as a captured scene, read in one go.
    init(engineBytes text: std.string) {
        self = String(text)
    }
}

/// The node types the engine has, read once, as the editor draws and offers them.
@MainActor
enum NodeTypeCatalog {
    private static var loaded: [EditorNodeType]?

    /// Every type `createNode` takes, aliases included, sorted by name. The engine has to be started.
    static var all: [EditorNodeType] {
        if let loaded { return loaded }
        let types = helix.core.listNodeTypeInfos().map { info in
            let name = String(engine: info.name)
            let english = String(engine: info.label)
            let englishTags = info.tags.map { String(engine: $0) }
            let label = NodeText.typeLabel(name, fallback: english)
            let tags = englishTags.map { NodeText.tag($0) }
            return EditorNodeType(
                name: name,
                label: label,
                tags: tags,
                description: NodeText.typeDescription(name, fallback: String(engine: info.description)),
                searchLabels: label == english ? [] : [english],
                searchTags: tags == englishTags ? [] : englishTags,
                inputs: info.inputs.enumerated().map { index, port in
                    EditorPortInfo(
                        label: NodeText.inputLabel(name, index: index, fallback: String(engine: port.label)),
                        multiInput: port.multiInput, optional: port.optional
                    )
                },
                outputCount: Int(info.outputCount)
            )
        }
        loaded = types
        return types
    }

    /// The types a person is offered to add: every type but the test ones. `all` still has them, since
    /// a scene that holds one has to draw it.
    static var offered: [EditorNodeType] {
        all.filter { !$0.isTest }
    }

    static var byName: [String: EditorNodeType] {
        Dictionary(all.map { ($0.name, $0) }, uniquingKeysWith: { first, _ in first })
    }
}

/// Copies the graph out of the engine.
@MainActor
enum EditorGraphLoader {
    static func current() -> EditorGraph {
        let nodes = helix.core.listNodes().map { node in
            EditorNode(
                id: node.id,
                type: String(engine: node.type),
                aliasType: String(engine: node.aliasType),
                name: String(engine: node.name),
                path: String(engine: node.path),
                position: CGPoint(x: CGFloat(node.x), y: CGFloat(node.y)),
                error: node.error.empty() ? nil : String(engineMessage: node.error),
                warnings: node.warnings.map { String(engineMessage: $0) }
            )
        }
        let links = helix.core.listLinks().map { link in
            EditorLink(
                sourceNode: link.sourceNode,
                sourceOutput: Int(link.sourceOutput),
                targetNode: link.targetNode,
                targetInput: Int(link.targetInput)
            )
        }
        let display = helix.core.displayNode()
        return EditorGraph(nodes: nodes, links: links, displayNode: display == 0 ? nil : display, types: NodeTypeCatalog.byName)
    }
}
