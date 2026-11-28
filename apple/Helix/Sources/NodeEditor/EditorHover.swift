import CoreGraphics
import Foundation

/// A port of a card: the end of a link, which the pointer can rest on to be told what it is.
enum EditorPort: Hashable, Sendable {
    /// An input of the type, by its place in the list of inputs. The multi input port is the last.
    case input(node: UInt64, index: Int)
    case output(node: UInt64, index: Int)

    var node: UInt64 {
        switch self {
        case .input(let node, _), .output(let node, _): node
        }
    }
}

/// What the pointer rests on in the editor, when it is not busy with a gesture.
struct EditorHover: Equatable, Sendable {
    var port: EditorPort?
    var node: UInt64?
    /// Whether the pointer is over the eye zone of that node.
    var eye = false
}

extension EditorLayout {
    /// How wide the strip at the left of a card is, which makes the node the one shown in the 3D
    /// view when it is clicked, in canvas units.
    static let eyeWidth: CGFloat = 24
    /// The zoom below which the eye is not drawn, and so does not take clicks either.
    static let eyeMinZoom: CGFloat = 0.5

    /// The strip of a card that is the eye: its whole height, from the left edge.
    static func eyeRect(at position: CGPoint) -> CGRect {
        let card = cardRect(at: position)
        return CGRect(x: card.minX, y: card.minY, width: eyeWidth, height: card.height)
    }

    /// Whether a point of the canvas is on the eye of a node, at a zoom that draws it.
    static func isOverEye(_ point: CGPoint, of node: EditorNode, zoom: CGFloat) -> Bool {
        zoom >= eyeMinZoom && eyeRect(at: node.position).contains(point)
    }

    /// The port a point of the canvas is close enough to, for its label to show. Cards on top win,
    /// and an output wins over an input where they meet. Unlike `hit`, it finds inputs that no link
    /// ends in, and it leaves the eye and the cards alone.
    static func port(at point: CGPoint, zoom: CGFloat, in graph: EditorGraph) -> EditorPort? {
        let grab = grabRadius / zoom
        for node in graph.nodes.reversed() {
            let type = graph.type(of: node)
            for index in 0..<max(type.outputCount, 0) {
                let port = outputPoint(of: node, type: type, index: index)
                if hypot(point.x - port.x, point.y - port.y) <= grab { return .output(node: node.id, index: index) }
            }
            for index in 0..<type.singleInputCount {
                let port = inputPoint(of: node, type: type, index: index, multiCount: 0)
                if hypot(point.x - port.x, point.y - port.y) <= grab { return .input(node: node.id, index: index) }
            }
            if let bar = multiInputBar(of: node, type: type),
               bar.insetBy(dx: -grab / 2, dy: -grab).contains(point) {
                return .input(node: node.id, index: type.inputs.count - 1)
            }
        }
        return nil
    }

    /// Where a port is, on the canvas, for its label to sit by it.
    static func point(of port: EditorPort, in graph: EditorGraph) -> CGPoint? {
        switch port {
        case .output(let id, let index):
            guard let node = graph.node(id) else { return nil }
            return outputPoint(of: node, type: graph.type(of: node), index: index)
        case .input(let id, let index):
            guard let node = graph.node(id) else { return nil }
            let type = graph.type(of: node)
            if type.hasMultiInput, index == type.inputs.count - 1, let bar = multiInputBar(of: node, type: type) {
                return CGPoint(x: bar.midX, y: bar.midY)
            }
            return inputPoint(of: node, type: type, index: index, multiCount: 0)
        }
    }

    /// What a port is for, in words: the label of an input, and "Output" for an output, numbered
    /// when the node has several, since the engine does not name its outputs.
    static func label(of port: EditorPort, in graph: EditorGraph) -> String {
        switch port {
        case .input(let id, let index):
            guard let node = graph.node(id) else { return "" }
            let inputs = graph.type(of: node).inputs
            return inputs.indices.contains(index) ? inputs[index].label : ""
        case .output(let id, let index):
            guard let node = graph.node(id) else { return "" }
            return outputLabel(index, of: graph.type(of: node).outputCount)
        }
    }

    static func outputLabel(_ index: Int, of count: Int) -> String {
        count > 1 ? String(localized: "Output \(index + 1)") : String(localized: "Output")
    }
}

extension NodeEditorModel {
    /// Works out what the pointer rests on. Nothing is hovered while a gesture is under way.
    func updateHover(at point: CGPoint?) {
        var next = EditorHover()
        if let point, interaction == .idle {
            let canvas = transform.canvasPoint(point)
            next.port = EditorLayout.port(at: canvas, zoom: transform.zoom, in: graph)
            switch EditorLayout.hit(at: canvas, zoom: transform.zoom, in: graph) {
            case .node(let id):
                next.node = id
                if let node = graph.node(id) { next.eye = EditorLayout.isOverEye(canvas, of: node, zoom: transform.zoom) }
            case .output(let id, _):
                next.node = id
            case .linkedInput(let link):
                next.node = link.targetNode
            case .link, .none:
                break
            }
            if next.node == nil, let port = next.port { next.node = port.node }
        }
        if next != hover { hover = next }
    }
}

extension NodeEditorModel {
    /// The eye of each card, where it is in the view, for its tooltip.
    func eyeAreas() -> [CGRect] {
        guard transform.zoom >= EditorLayout.eyeMinZoom else { return [] }
        return graph.nodes.map { transform.viewRect(EditorLayout.eyeRect(at: displayPosition(of: $0))) }
    }
}
