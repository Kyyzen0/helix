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

    /// Where the two nodes of a skeleton chain go under an import: the skin deform two rows down, in
    /// line with it (its geometry input comes straight from the import), and the pose one row
    /// down and one card to the right, so that the straight link does not run behind it. A spot
    /// another card holds is passed to the right, as for any new node, and the pose never takes
    /// the spot the deform was given.
    static func skeletonChain(below source: CGPoint, in graph: EditorGraph) -> (pose: CGPoint, deform: CGPoint) {
        let row = EditorLayout.chainSpacing
        let deform = free(CGPoint(x: source.x, y: source.y + 2 * row), in: graph)
        let pose = free(
            CGPoint(x: source.x + EditorLayout.cardSize.width + 12, y: source.y + row), in: graph, alsoAvoiding: [deform]
        )
        return (pose, deform)
    }

    private static func free(_ position: CGPoint, in graph: EditorGraph, alsoAvoiding extra: [CGPoint] = []) -> CGPoint {
        var candidate = position
        let step = EditorLayout.cardSize.width + 12
        for _ in 0..<64 {
            let card = EditorLayout.cardRect(at: candidate).insetBy(dx: 1, dy: 1)
            let taken = graph.nodes.map(\.position) + extra
            guard taken.contains(where: { EditorLayout.cardRect(at: $0).intersects(card) }) else {
                return candidate
            }
            candidate.x += step
        }
        return position
    }
}
