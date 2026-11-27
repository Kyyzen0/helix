import CoreGraphics

/// Where a node made from the palette goes, and what it is wired to.
enum NodePlacement {
    struct Result {
        /// The centre of the new card, in canvas units.
        var position: CGPoint
        /// The output the first input of the new node takes, or nil when it is wired to nothing.
        var wiredFrom: (node: UInt64, output: Int)?
    }

    /// Chooses the place of a new node, in the way of Houdini: when exactly one node is
    /// selected and has an output, and the new type has an input to take it, the new node goes
    /// 120 under the selected one and is wired to it. Otherwise it goes under the pointer.
    ///
    /// A spot that another card already holds is passed to the right, since a second node
    /// added under the same node would otherwise sit exactly on the first.
    static func place(
        _ type: EditorNodeType,
        at pointer: CGPoint,
        selection: Set<UInt64>,
        in graph: EditorGraph
    ) -> Result {
        guard selection.count == 1, let id = selection.first, let selected = graph.node(id),
              graph.type(of: selected).outputCount > 0, !type.inputs.isEmpty
        else {
            return Result(position: free(pointer, in: graph), wiredFrom: nil)
        }
        let below = CGPoint(x: selected.position.x, y: selected.position.y + EditorLayout.chainSpacing)
        return Result(position: free(below, in: graph), wiredFrom: (selected.id, 0))
    }

    private static func free(_ position: CGPoint, in graph: EditorGraph) -> CGPoint {
        var candidate = position
        let step = EditorLayout.cardSize.width + 12
        for _ in 0..<64 {
            let card = EditorLayout.cardRect(at: candidate).insetBy(dx: 1, dy: 1)
            guard graph.nodes.contains(where: { EditorLayout.cardRect(at: $0.position).intersects(card) }) else {
                return candidate
            }
            candidate.x += step
        }
        return position
    }
}
