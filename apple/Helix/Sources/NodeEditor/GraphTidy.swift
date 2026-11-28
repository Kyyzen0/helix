import CoreGraphics
import Foundation

/// Where the nodes of a graph go when it is tidied: in rows from the sources down, the way the
/// links run (an output on the bottom edge of a card, an input on the top edge).
///
/// It is a pure function of the graph, so every rule of it is tested with no editor and no engine.
enum GraphTidy {
    /// The room between two cards of a row.
    static let gap: CGFloat = 36
    /// The room between two pieces of the graph that no link joins.
    static let pieceGap: CGFloat = 72
    /// How many times the order of the rows is improved, down and then up.
    static let sweeps = 4

    /// The new positions of the nodes, or an empty list when none would move. `ids` limits the
    /// tidy to those nodes (the links between them count, the others do not), and the result is
    /// anchored at the top left of where they were, so the graph does not jump across the canvas.
    static func moves(for graph: EditorGraph, among ids: Set<UInt64>? = nil) -> [NodeMove] {
        let nodes = graph.nodes.filter { ids?.contains($0.id) ?? true }
        guard nodes.count >= 2, let original = graph.bounds(of: ids) else { return [] }
        let known = Set(nodes.map(\.id))
        let links = graph.links.filter {
            known.contains($0.sourceNode) && known.contains($0.targetNode) && $0.sourceNode != $0.targetNode
        }

        // Neighbours, each once however many links join the pair.
        var parents: [UInt64: [UInt64]] = [:]
        var children: [UInt64: [UInt64]] = [:]
        for link in links {
            if !(parents[link.targetNode]?.contains(link.sourceNode) ?? false) {
                parents[link.targetNode, default: []].append(link.sourceNode)
                children[link.sourceNode, default: []].append(link.targetNode)
            }
        }
        // Where a link lands along the top of its card, so the parent of the first input leans left.
        var inputShift: [UInt64: [UInt64: CGFloat]] = [:]
        for link in links {
            let target = graph.node(link.targetNode)
            let count = max(target.map { graph.type(of: $0).inputs.count } ?? 1, 1)
            let index = min(link.targetInput, count - 1)
            inputShift[link.targetNode, default: [:]][link.sourceNode] = (CGFloat(index) - CGFloat(count - 1) / 2) * 0.35
        }

        let pieces = connectedPieces(of: nodes, parents: parents, children: children)
        var placed: [UInt64: CGPoint] = [:]
        var cursor: CGFloat = 0
        for piece in pieces {
            let (rows, itemParents, itemChildren) = orderedRows(of: piece, parents: parents, children: children, inputShift: inputShift)
            let centres = straightened(rows, parents: itemParents, children: itemChildren)
            var left = CGFloat.infinity, right = -CGFloat.infinity
            for row in rows {
                for item in row {
                    guard let x = centres[item.key] else { continue }
                    left = min(left, x - item.width / 2)
                    right = max(right, x + item.width / 2)
                }
            }
            for (rowIndex, row) in rows.enumerated() {
                for item in row {
                    if let node = item.node, let x = centres[item.key] {
                        placed[node.id] = CGPoint(x: cursor + x - left, y: CGFloat(rowIndex) * EditorLayout.chainSpacing)
                    }
                }
            }
            cursor += max(right - left, EditorLayout.cardSize.width) + pieceGap
        }

        // Anchor the result at the top left of where the nodes were.
        var result: CGRect?
        for node in nodes {
            guard let centre = placed[node.id] else { continue }
            let card = EditorLayout.cardRect(at: centre)
            result = result.map { $0.union(card) } ?? card
        }
        guard let result else { return [] }
        let shift = CGPoint(x: original.minX - result.minX, y: original.minY - result.minY)
        return nodes.compactMap { node in
            guard let centre = placed[node.id] else { return nil }
            let target = CGPoint(x: centre.x + shift.x, y: centre.y + shift.y)
            guard abs(target.x - node.position.x) > 0.01 || abs(target.y - node.position.y) > 0.01 else { return nil }
            return NodeMove(id: node.id, position: target)
        }
    }

    // MARK: Pieces

    /// The nodes grouped by the links that join them, whatever way the links run. The pieces come
    /// in the order of their leftmost node, and each keeps the order of the graph.
    private static func connectedPieces(
        of nodes: [EditorNode],
        parents: [UInt64: [UInt64]],
        children: [UInt64: [UInt64]]
    ) -> [[EditorNode]] {
        let byID = Dictionary(uniqueKeysWithValues: nodes.map { ($0.id, $0) })
        var seen = Set<UInt64>()
        var pieces: [[EditorNode]] = []
        for node in nodes where !seen.contains(node.id) {
            var piece: [EditorNode] = []
            var stack = [node.id]
            seen.insert(node.id)
            while let id = stack.popLast() {
                if let found = byID[id] { piece.append(found) }
                for neighbour in (parents[id] ?? []) + (children[id] ?? []) where seen.insert(neighbour).inserted {
                    stack.append(neighbour)
                }
            }
            pieces.append(piece.sorted(by: { isBefore($0, $1) }))
        }
        return pieces.sorted { left, right in
            let a = left.map(\.position.x).min() ?? 0, b = right.map(\.position.x).min() ?? 0
            return a != b ? a < b : (left.first?.id ?? 0) < (right.first?.id ?? 0)
        }
    }

    /// Left to right as they were, then top to bottom, so a tie keeps the person's own order.
    private static func isBefore(_ left: EditorNode, _ right: EditorNode) -> Bool {
        if left.position.x != right.position.x { return left.position.x < right.position.x }
        if left.position.y != right.position.y { return left.position.y < right.position.y }
        return left.id < right.id
    }

    // MARK: Rows

    /// What sits in a row: a card, or a place a long link passes through. A link that spans several
    /// rows is cut into one such place per row it crosses, which orders and spaces like a card but
    /// is narrow, so the cards of the rows in between are pushed aside and the wire runs between them.
    private struct Item {
        var key: Int
        var node: EditorNode?
        var width: CGFloat { node == nil ? wireWidth : EditorLayout.cardSize.width }
    }

    /// The room between two neighbours of a row: a wire needs less than two cards do.
    private static func space(_ left: Item, _ right: Item) -> CGFloat {
        left.node == nil || right.node == nil ? 20 : gap
    }

    /// The room a wire takes in a row.
    static let wireWidth: CGFloat = 16

    /// The items of a piece in rows: a node sits one row below the lowest of its parents, so a
    /// link always runs down. Within a row the order follows the average place of the neighbours,
    /// improved by a few sweeps down and up, which untangles most crossings.
    private static func orderedRows(
        of piece: [EditorNode],
        parents: [UInt64: [UInt64]],
        children: [UInt64: [UInt64]],
        inputShift: [UInt64: [UInt64: CGFloat]]
    ) -> (rows: [[Item]], parents: [Int: [Int]], children: [Int: [Int]]) {
        let ids = Set(piece.map(\.id))
        var depth: [UInt64: Int] = [:]
        var waiting: [UInt64: Int] = [:]
        var queue: [UInt64] = []
        for node in piece {
            let count = (parents[node.id] ?? []).filter(ids.contains).count
            waiting[node.id] = count
            if count == 0 { queue.append(node.id); depth[node.id] = 0 }
        }
        var head = 0
        while head < queue.count {
            let id = queue[head]
            head += 1
            for child in children[id] ?? [] where ids.contains(child) {
                depth[child] = max(depth[child] ?? 0, (depth[id] ?? 0) + 1)
                waiting[child, default: 0] -= 1
                if waiting[child] == 0 { queue.append(child) }
            }
        }
        // A cycle (the engine refuses them, but a file may hold one) leaves its nodes in the first row.
        for node in piece where depth[node.id] == nil { depth[node.id] = 0 }

        let rowCount = (depth.values.max() ?? 0) + 1
        var rows = [[Item]](repeating: [], count: rowCount)
        var keyOf: [UInt64: Int] = [:]
        for (index, node) in piece.enumerated() {
            keyOf[node.id] = index
            rows[depth[node.id] ?? 0].append(Item(key: index, node: node))
        }

        // The links between neighbouring rows, with a place in each row a long link crosses.
        var itemParents: [Int: [Int]] = [:]
        var itemChildren: [Int: [Int]] = [:]
        var shift: [Int: [Int: CGFloat]] = [:]
        var nextKey = piece.count
        for node in piece {
            for parent in parents[node.id] ?? [] where ids.contains(parent) {
                guard let top = depth[parent], let bottom = depth[node.id], bottom > top,
                      let parentKey = keyOf[parent], let childKey = keyOf[node.id] else { continue }
                var previous = parentKey
                for row in (top + 1)..<bottom {
                    rows[row].append(Item(key: nextKey, node: nil))
                    itemParents[nextKey, default: []].append(previous)
                    itemChildren[previous, default: []].append(nextKey)
                    previous = nextKey
                    nextKey += 1
                }
                itemParents[childKey, default: []].append(previous)
                itemChildren[previous, default: []].append(childKey)
                shift[childKey, default: [:]][previous] = inputShift[node.id]?[parent] ?? 0
            }
        }

        // The place of an item in its row, centred so rows of different sizes compare.
        func columns(_ rows: [[Item]]) -> [Int: CGFloat] {
            var result: [Int: CGFloat] = [:]
            for row in rows {
                for (index, item) in row.enumerated() { result[item.key] = CGFloat(index) - CGFloat(row.count - 1) / 2 }
            }
            return result
        }

        for _ in 0..<sweeps {
            var column = columns(rows)
            for rowIndex in rows.indices.dropFirst() {
                rows[rowIndex] = sorted(rows[rowIndex], column: column) { item in
                    let near = (itemParents[item.key] ?? []).compactMap { column[$0] }
                    return near.isEmpty ? nil : near.reduce(0, +) / CGFloat(near.count)
                }
                column = columns(rows)
            }
            for rowIndex in rows.indices.dropLast().reversed() {
                rows[rowIndex] = sorted(rows[rowIndex], column: column) { item in
                    let near = (itemChildren[item.key] ?? []).compactMap { child in
                        column[child].map { $0 + (shift[child]?[item.key] ?? 0) }
                    }
                    return near.isEmpty ? nil : near.reduce(0, +) / CGFloat(near.count)
                }
                column = columns(rows)
            }
        }
        return (rows, itemParents, itemChildren)
    }

    /// The x of the centre of every item, with the wires as straight as the cards allow. Each pass
    /// pulls an item towards the average x of its neighbours, then pushes the row apart where items
    /// would touch, keeping their order and moving them as little as it can (so a card pushed aside
    /// takes the cards next to it with it, and a long wire keeps its line between them).
    private static func straightened(
        _ rows: [[Item]],
        parents: [Int: [Int]],
        children: [Int: [Int]]
    ) -> [Int: CGFloat] {
        var x: [Int: CGFloat] = [:]
        for row in rows {
            var cursor: CGFloat = 0
            for (index, item) in row.enumerated() {
                if index > 0 { cursor += space(row[index - 1], item) }
                x[item.key] = cursor + item.width / 2
                cursor += item.width
            }
            let total = cursor
            for item in row { x[item.key]! -= total / 2 }
        }
        for pass in 0..<12 {
            let order = pass % 2 == 0 ? Array(rows.indices) : rows.indices.reversed()
            for rowIndex in order {
                let row = rows[rowIndex]
                // Where each item would like to be.
                var wanted: [CGFloat] = []
                for item in row {
                    let near = ((pass % 2 == 0 ? parents[item.key] : children[item.key]) ?? []) + ((pass % 2 == 0 ? children[item.key] : parents[item.key]) ?? [])
                    let xs = near.compactMap { x[$0] }
                    wanted.append(xs.isEmpty ? x[item.key] ?? 0 : xs.reduce(0, +) / CGFloat(xs.count))
                }
                // Least movement that keeps the order and the room: pool adjacent violators on the
                // wishes with the room taken off.
                var offset: [CGFloat] = []
                var run: CGFloat = 0
                for (index, item) in row.enumerated() {
                    if index > 0 { run += (row[index - 1].width + item.width) / 2 + space(row[index - 1], item) }
                    offset.append(run)
                }
                var blocks: [(sum: CGFloat, count: CGFloat, first: Int, last: Int)] = []
                for index in row.indices {
                    blocks.append((wanted[index] - offset[index], 1, index, index))
                    while blocks.count > 1, blocks[blocks.count - 2].sum / blocks[blocks.count - 2].count > blocks[blocks.count - 1].sum / blocks[blocks.count - 1].count {
                        let top = blocks.removeLast()
                        let below = blocks.removeLast()
                        blocks.append((below.sum + top.sum, below.count + top.count, below.first, top.last))
                    }
                }
                for block in blocks {
                    for index in block.first...block.last { x[row[index].key] = block.sum / block.count + offset[index] }
                }
            }
        }
        return x
    }

    /// A row ordered by where its items want to be. An item with no neighbour to follow wants to
    /// stay where it is, and a tie keeps the order it had.
    private static func sorted(
        _ row: [Item],
        column: [Int: CGFloat],
        wanted: (Item) -> CGFloat?
    ) -> [Item] {
        var keyed: [(index: Int, item: Item, key: CGFloat)] = []
        for (index, item) in row.enumerated() {
            let key: CGFloat = wanted(item) ?? column[item.key] ?? 0
            keyed.append((index, item, key))
        }
        keyed.sort { left, right in
            left.key != right.key ? left.key < right.key : left.index < right.index
        }
        return keyed.map(\.item)
    }
}
