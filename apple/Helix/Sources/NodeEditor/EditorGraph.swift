import CoreGraphics
import Foundation

// The node editor works on plain values, copied out of the engine. Nothing in the files that
// draw, hit test or lay out the graph imports the facade, so all of it can be tested without
// an engine.

/// One input port of a node type.
struct EditorPortInfo: Hashable, Sendable {
    var label: String
    /// Whether the port takes any number of inputs. Only the last port of a type can.
    var multiInput = false
    var optional = false
}

/// What the editor needs to know about a node type to draw a card and to offer it in the palette.
struct EditorNodeType: Hashable, Sendable {
    /// The full name `createNode` takes, such as `helix::cube`.
    var name: String
    /// The name a person reads, such as "Cube".
    var label: String
    var tags: [String] = []
    var inputs: [EditorPortInfo] = []
    var outputCount = 1

    /// What a card shows for a node whose type is not known, so a scene still draws.
    static func unknown(_ name: String) -> EditorNodeType {
        EditorNodeType(name: name, label: name, inputs: [EditorPortInfo(label: "")], outputCount: 1)
    }

    var hasMultiInput: Bool { inputs.last?.multiInput ?? false }

    /// The number of ports that take one input each.
    var singleInputCount: Int { hasMultiInput ? inputs.count - 1 : inputs.count }
}

/// A node of the graph, where the engine keeps it.
struct EditorNode: Hashable, Identifiable, Sendable {
    var id: UInt64
    /// The full type name, such as `helix::cube`.
    var type: String
    var name: String
    /// The path in the scene, such as `/cube1`. A restore of the scene gives nodes new ids, so
    /// the path is what finds a node again.
    var path: String
    /// The centre of the card, in canvas units, y growing downward.
    var position: CGPoint
}

/// A link from the output of a node into an input of another.
struct EditorLink: Hashable, Sendable {
    var sourceNode: UInt64
    var sourceOutput: Int
    var targetNode: UInt64
    var targetInput: Int
}

/// The graph as the editor draws it.
struct EditorGraph: Sendable {
    /// The nodes in the order they are drawn, the last one on top.
    private(set) var nodes: [EditorNode] = []
    private(set) var links: [EditorLink] = []
    var displayNode: UInt64?
    private(set) var types: [String: EditorNodeType] = [:]
    private var indexByID: [UInt64: Int] = [:]

    init() {}

    init(nodes: [EditorNode], links: [EditorLink], displayNode: UInt64?, types: [String: EditorNodeType]) {
        self.nodes = nodes
        self.links = links
        self.displayNode = displayNode
        self.types = types
        for (index, node) in nodes.enumerated() { indexByID[node.id] = index }
    }

    func node(_ id: UInt64) -> EditorNode? {
        indexByID[id].map { nodes[$0] }
    }

    func type(of node: EditorNode) -> EditorNodeType {
        types[node.type] ?? .unknown(node.type)
    }

    /// The links that end in a node.
    func links(into node: UInt64) -> [EditorLink] {
        links.filter { $0.targetNode == node }
    }

    /// The link that ends in an input of a node, if there is one.
    func link(into node: UInt64, input: Int) -> EditorLink? {
        links.first { $0.targetNode == node && $0.targetInput == input }
    }

    /// The input a new link into the multi input port of a node takes: the one after the last
    /// that is used, or the first of the port.
    func nextMultiInput(of node: EditorNode) -> Int {
        let type = type(of: node)
        let first = type.singleInputCount
        let used = links(into: node.id).map(\.targetInput).filter { $0 >= first }
        return (used.max() ?? (first - 1)) + 1
    }

    /// The bounds of every card, or nil for an empty graph.
    func bounds(of ids: Set<UInt64>? = nil) -> CGRect? {
        var result: CGRect?
        for node in nodes where ids?.contains(node.id) ?? true {
            let card = EditorLayout.cardRect(at: node.position)
            result = result.map { $0.union(card) } ?? card
        }
        return result
    }
}
