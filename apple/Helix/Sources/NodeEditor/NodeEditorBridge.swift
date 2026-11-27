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
            EditorNodeType(
                name: String(engine: info.name),
                label: String(engine: info.label),
                tags: info.tags.map { String(engine: $0) },
                inputs: info.inputs.map {
                    EditorPortInfo(label: String(engine: $0.label), multiInput: $0.multiInput, optional: $0.optional)
                },
                outputCount: Int(info.outputCount)
            )
        }
        loaded = types
        return types
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
                name: String(engine: node.name),
                path: String(engine: node.path),
                position: CGPoint(x: CGFloat(node.x), y: CGFloat(node.y)),
                error: node.error.empty() ? nil : String(engine: node.error),
                warnings: node.warnings.map { String(engine: $0) }
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
