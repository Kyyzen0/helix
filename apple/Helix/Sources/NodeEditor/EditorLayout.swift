import CoreGraphics
import Foundation

/// Where the parts of a node are, and what a point of the canvas touches.
///
/// The card and the ports keep the geometry of the Qt editor, so a scene laid out there looks the
/// same here: a card of 148 by 46 centred on the position of the node, inputs along its top edge
/// and outputs along its bottom edge, `width * (i + 1) / (n + 1)` from the left for the port `i`
/// of `n`. Links are vertical cubic curves from an output down to an input.
enum EditorLayout {
    static let cardSize = CGSize(width: 148, height: 46)
    /// How far apart a node and the one chained under it sit.
    static let chainSpacing: CGFloat = 120
    static let portRadius: CGFloat = 4.5

    /// How close the pointer must be to a port to grab it, and to a port to snap a link onto
    /// it, in points of the screen, so the targets keep their size at every zoom.
    static let grabRadius: CGFloat = 9
    static let snapRadius: CGFloat = 26
    /// How close the pointer must be to a link to pick it, in points of the screen.
    static let linkTolerance: CGFloat = 6

    static func cardRect(at position: CGPoint) -> CGRect {
        CGRect(
            x: position.x - cardSize.width / 2,
            y: position.y - cardSize.height / 2,
            width: cardSize.width,
            height: cardSize.height
        )
    }

    // MARK: Ports

    /// The width one port covers on the edge of a card with `count` ports.
    private static func portWidth(_ count: Int) -> CGFloat {
        cardSize.width / CGFloat(count + 1)
    }

    /// The point of an output on the bottom edge of a card.
    static func outputPoint(of node: EditorNode, type: EditorNodeType, index: Int) -> CGPoint {
        let card = cardRect(at: node.position)
        let count = max(type.outputCount, 1)
        return CGPoint(x: card.minX + portWidth(count) * CGFloat(index + 1), y: card.maxY)
    }

    /// The bar of the multi input port, along the top edge of the card, or nil when the type
    /// has none. It is wider than the share of one port, since it takes any number of links.
    static func multiInputBar(of node: EditorNode, type: EditorNodeType) -> CGRect? {
        guard type.hasMultiInput else { return nil }
        let card = cardRect(at: node.position)
        let count = type.inputs.count
        let width = min(portWidth(count) * 1.5, cardSize.width)
        let centre = card.minX + portWidth(count) * CGFloat(count)
        return CGRect(x: centre - width / 2, y: card.minY - 3, width: width, height: 6)
    }

    /// The point where the link into an input of a node ends.
    ///
    /// Single ports sit at their share of the edge. The links of the multi input port spread
    /// evenly along its bar, in the order of their inputs, and `multiCount` is how many spots
    /// the bar is shared by.
    static func inputPoint(of node: EditorNode, type: EditorNodeType, index: Int, multiCount: Int) -> CGPoint {
        let card = cardRect(at: node.position)
        let single = type.singleInputCount
        if index < single || !type.hasMultiInput {
            return CGPoint(x: card.minX + portWidth(type.inputs.count) * CGFloat(index + 1), y: card.minY)
        }
        guard let bar = multiInputBar(of: node, type: type) else {
            return CGPoint(x: card.midX, y: card.minY)
        }
        let spots = max(multiCount, index - single + 1)
        return CGPoint(
            x: bar.minX + bar.width * CGFloat(index - single + 1) / CGFloat(spots + 1),
            y: card.minY
        )
    }

    /// How many spots the multi input bar of a node is shared by: its links, or the highest
    /// input in use when the inputs are not consecutive.
    static func multiSpots(of node: EditorNode, type: EditorNodeType, graph: EditorGraph) -> Int {
        guard type.hasMultiInput else { return 0 }
        let single = type.singleInputCount
        let used = graph.links(into: node.id).map(\.targetInput).filter { $0 >= single }
        return max(used.count, (used.max().map { $0 - single + 1 } ?? 0))
    }

    /// Both ends of a link, or nil when a node of the link is gone.
    static func endpoints(of link: EditorLink, in graph: EditorGraph) -> (from: CGPoint, to: CGPoint)? {
        guard let source = graph.node(link.sourceNode), let target = graph.node(link.targetNode) else { return nil }
        let sourceType = graph.type(of: source)
        let targetType = graph.type(of: target)
        return (
            outputPoint(of: source, type: sourceType, index: link.sourceOutput),
            inputPoint(
                of: target,
                type: targetType,
                index: link.targetInput,
                multiCount: multiSpots(of: target, type: targetType, graph: graph)
            )
        )
    }

    // MARK: Links

    /// The two inner control points of the curve from an output down to an input. They sit
    /// straight below the output and straight above the input, so the link leaves and arrives
    /// vertically, and further out when the input is far below, or above the output.
    static func controlPoints(from start: CGPoint, to end: CGPoint) -> (CGPoint, CGPoint) {
        let reach = max(40, abs(end.y - start.y) * 0.5)
        return (CGPoint(x: start.x, y: start.y + reach), CGPoint(x: end.x, y: end.y - reach))
    }

    /// Points along the curve, for hit testing.
    static func curvePoints(from start: CGPoint, to end: CGPoint, steps: Int = 24) -> [CGPoint] {
        let (control1, control2) = controlPoints(from: start, to: end)
        return (0...steps).map { step in
            let t = CGFloat(step) / CGFloat(steps)
            let u = 1 - t
            let a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t
            return CGPoint(
                x: a * start.x + b * control1.x + c * control2.x + d * end.x,
                y: a * start.y + b * control1.y + c * control2.y + d * end.y
            )
        }
    }

    static func distance(from point: CGPoint, toSegment a: CGPoint, _ b: CGPoint) -> CGFloat {
        let dx = b.x - a.x, dy = b.y - a.y
        let lengthSquared = dx * dx + dy * dy
        guard lengthSquared > 0 else { return hypot(point.x - a.x, point.y - a.y) }
        let t = min(max(((point.x - a.x) * dx + (point.y - a.y) * dy) / lengthSquared, 0), 1)
        return hypot(point.x - (a.x + t * dx), point.y - (a.y + t * dy))
    }

    /// The distance from a point to a link, along its curve.
    static func distance(from point: CGPoint, to link: EditorLink, in graph: EditorGraph) -> CGFloat? {
        guard let (start, end) = endpoints(of: link, in: graph) else { return nil }
        // A curve stays inside the box of its four control points, so a link far from the
        // point is dismissed before it is sampled.
        let (control1, control2) = controlPoints(from: start, to: end)
        let xs = [start.x, end.x, control1.x, control2.x], ys = [start.y, end.y, control1.y, control2.y]
        let box = CGRect(x: xs.min()!, y: ys.min()!, width: xs.max()! - xs.min()!, height: ys.max()! - ys.min()!)
        if point.x < box.minX - 100 || point.x > box.maxX + 100 || point.y < box.minY - 100 || point.y > box.maxY + 100 {
            return nil
        }
        let points = curvePoints(from: start, to: end)
        return zip(points, points.dropFirst()).map { distance(from: point, toSegment: $0, $1) }.min()
    }

    // MARK: Hit testing

    /// What a point of the canvas touches.
    enum Hit: Equatable {
        /// An output port: a press starts a new link.
        case output(node: UInt64, index: Int)
        /// The end of a link at an input port: a press takes the link off.
        case linkedInput(EditorLink)
        case node(UInt64)
        case link(EditorLink)
        case none
    }

    /// What the point touches, with `zoom` telling how large a point of the screen is on the
    /// canvas. Ports come first, since they sit on the edge of a card and past it. Then cards,
    /// the one drawn last on top, then links.
    static func hit(at point: CGPoint, zoom: CGFloat, in graph: EditorGraph) -> Hit {
        let grab = grabRadius / zoom
        for node in graph.nodes.reversed() {
            let type = graph.type(of: node)
            for index in 0..<max(type.outputCount, 0) {
                let port = outputPoint(of: node, type: type, index: index)
                if hypot(point.x - port.x, point.y - port.y) <= grab { return .output(node: node.id, index: index) }
            }
            for link in graph.links(into: node.id) {
                let end = inputPoint(
                    of: node,
                    type: type,
                    index: link.targetInput,
                    multiCount: multiSpots(of: node, type: type, graph: graph)
                )
                if hypot(point.x - end.x, point.y - end.y) <= grab { return .linkedInput(link) }
            }
        }
        for node in graph.nodes.reversed() where cardRect(at: node.position).contains(point) {
            return .node(node.id)
        }
        let tolerance = linkTolerance / zoom
        var best: (link: EditorLink, distance: CGFloat)?
        for link in graph.links {
            guard let distance = distance(from: point, to: link, in: graph), distance <= tolerance else { continue }
            if best == nil || distance < best!.distance { best = (link, distance) }
        }
        return best.map { .link($0.link) } ?? .none
    }

    // MARK: Snapping

    /// An input a dragged link can land on.
    struct InputTarget: Equatable {
        var node: UInt64
        /// The input the link takes: a single port's, or the next free one of the multi input port.
        var input: Int
        /// Where the link ends, on the canvas.
        var point: CGPoint
        var label: String
    }

    /// The input nearest to a point, within the snap radius, on a node other than the one the
    /// link leaves. A point over a card snaps to the port of that card nearest in x, since a
    /// card is a larger target than its ports.
    static func snapTarget(
        near point: CGPoint,
        zoom: CGFloat,
        in graph: EditorGraph,
        excluding source: UInt64
    ) -> InputTarget? {
        let radius = snapRadius / zoom
        var best: (target: InputTarget, distance: CGFloat)?

        for node in graph.nodes.reversed() where node.id != source {
            let type = graph.type(of: node)
            guard !type.inputs.isEmpty else { continue }
            let card = cardRect(at: node.position)
            let overCard = card.insetBy(dx: -4, dy: -4).contains(point)

            var candidates: [InputTarget] = []
            for index in 0..<type.singleInputCount {
                candidates.append(InputTarget(
                    node: node.id,
                    input: index,
                    point: inputPoint(of: node, type: type, index: index, multiCount: 0),
                    label: type.inputs[index].label
                ))
            }
            if type.hasMultiInput {
                let next = graph.nextMultiInput(of: node)
                let spots = multiSpots(of: node, type: type, graph: graph) + 1
                candidates.append(InputTarget(
                    node: node.id,
                    input: next,
                    point: inputPoint(of: node, type: type, index: next, multiCount: spots),
                    label: type.inputs[type.inputs.count - 1].label
                ))
            }

            for candidate in candidates {
                let distance = overCard
                    ? abs(candidate.point.x - point.x) / 1000
                    : hypot(candidate.point.x - point.x, candidate.point.y - point.y)
                if !overCard && distance > radius { continue }
                // A card under the pointer wins over a port of another card nearby.
                let rank = overCard ? distance - 1_000_000 : distance
                if best == nil || rank < best!.distance { best = (candidate, rank) }
            }
        }
        return best?.target
    }

    // MARK: Marquee

    /// The nodes whose card the rectangle of the canvas touches.
    static func nodes(in rect: CGRect, graph: EditorGraph) -> [UInt64] {
        graph.nodes.filter { cardRect(at: $0.position).intersects(rect) }.map(\.id)
    }
}
