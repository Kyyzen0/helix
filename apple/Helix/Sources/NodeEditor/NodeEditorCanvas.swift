import AppKit
import SwiftUI

/// The drawing of the node editor: the grid, the links, the cards and the gesture in progress.
///
/// A `Canvas` draws it, so the same view fills the window and, through `ImageRenderer`, a picture
/// for a check without a window. It only draws. Events come from `NodeEditorEvents`, which sits
/// over it.
struct NodeEditorCanvas: View {
    @ObservedObject var model: NodeEditorModel
    @Environment(\.theme) private var theme

    var body: some View {
        Canvas(rendersAsynchronously: false) { context, size in
            NodeEditorPainter(model: model, colors: theme, size: size).paint(&context)
        }
        .accessibilityLabel(Text("Node editor"))
    }
}

/// Draws one frame of the editor.
@MainActor
struct NodeEditorPainter {
    let model: NodeEditorModel
    let colors: ThemeColors
    let size: CGSize

    private var graph: EditorGraph { model.graph }
    private var transform: ViewTransform { model.transform }
    private var zoom: CGFloat { transform.zoom }

    private var background: Color { colors.nodeBackground }
    private var cardFill: Color { colors.surface }
    private var cardStroke: Color { colors.nodeCardStroke }
    private var linkColor: Color { colors.nodeLink }
    // Opaque, so a link that ends in a port does not show through it.
    private var portFill: Color { colors.nodePort }
    private var accent: Color { colors.accent }

    func paint(_ context: inout GraphicsContext) {
        context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(background))
        paintGrid(&context)
        paintLinks(&context)
        paintNodes(&context)
        paintGesture(&context)
        if graph.nodes.isEmpty { paintEmptyHint(&context) }
    }

    // MARK: Grid

    private func paintGrid(_ context: inout GraphicsContext) {
        // The dots thin out as the view zooms away, so they never turn into a haze.
        var spacing: CGFloat = 24
        while spacing * zoom < 9 { spacing *= 2 }
        let visible = transform.canvasRect(CGRect(origin: .zero, size: size))
        let startX = (visible.minX / spacing).rounded(.down) * spacing
        let startY = (visible.minY / spacing).rounded(.down) * spacing
        let dot = max(1, 1.2 * min(zoom, 1.5))
        var dots = Path()
        var x = startX
        while x <= visible.maxX {
            var y = startY
            while y <= visible.maxY {
                let point = transform.viewPoint(CGPoint(x: x, y: y))
                dots.addRect(CGRect(x: point.x - dot / 2, y: point.y - dot / 2, width: dot, height: dot))
                y += spacing
            }
            x += spacing
        }
        context.fill(dots, with: .color(colors.nodeGrid))
    }

    // MARK: Links

    private func curve(from start: CGPoint, to end: CGPoint) -> Path {
        let (control1, control2) = EditorLayout.controlPoints(from: start, to: end)
        var path = Path()
        path.move(to: transform.viewPoint(start))
        path.addCurve(
            to: transform.viewPoint(end),
            control1: transform.viewPoint(control1),
            control2: transform.viewPoint(control2)
        )
        return path
    }

    private var detachedLink: EditorLink? {
        if case .linking(let drag) = model.interaction, drag.moved { return drag.detached }
        return nil
    }

    private func paintLinks(_ context: inout GraphicsContext) {
        let width = min(max(1.6 * zoom, 1), 3.2)
        let hidden = detachedLink
        for link in graph.links where link != hidden && !model.selectedLinks.contains(link) {
            guard let (start, end) = displayEndpoints(of: link) else { continue }
            context.stroke(curve(from: start, to: end), with: .color(linkColor), lineWidth: width)
        }
        for link in model.selectedLinks where link != hidden {
            guard let (start, end) = displayEndpoints(of: link) else { continue }
            context.stroke(curve(from: start, to: end), with: .color(accent), lineWidth: width + 1.5)
        }
    }

    /// The ends of a link, with the cards that are being dragged where the pointer has them.
    private func displayEndpoints(of link: EditorLink) -> (CGPoint, CGPoint)? {
        guard let source = graph.node(link.sourceNode), let target = graph.node(link.targetNode) else { return nil }
        var movedSource = source
        movedSource.position = model.displayPosition(of: source)
        var movedTarget = target
        movedTarget.position = model.displayPosition(of: target)
        let targetType = graph.type(of: target)
        return (
            EditorLayout.outputPoint(of: movedSource, type: graph.type(of: source), index: link.sourceOutput),
            EditorLayout.inputPoint(
                of: movedTarget,
                type: targetType,
                index: link.targetInput,
                multiCount: EditorLayout.multiSpots(of: target, type: targetType, graph: graph)
            )
        )
    }

    // MARK: Nodes

    private func paintNodes(_ context: inout GraphicsContext) {
        let screen = CGRect(origin: .zero, size: size).insetBy(dx: -40, dy: -40)
        for node in graph.nodes {
            var shown = node
            shown.position = model.displayPosition(of: node)
            let cardInView = transform.viewRect(EditorLayout.cardRect(at: shown.position))
            if !cardInView.insetBy(dx: -10, dy: -10).intersects(screen) { continue }
            paint(shown, in: cardInView, &context)
        }
    }

    private func paint(_ node: EditorNode, in card: CGRect, _ context: inout GraphicsContext) {
        let type = graph.type(of: node)
        let selected = model.selectedNodes.contains(node.id)
        let isDisplay = graph.displayNode == node.id
        let radius = 7 * zoom
        let shape = Path(roundedRect: card, cornerRadius: radius)

        context.fill(shape, with: .color(cardFill))
        if selected { context.fill(shape, with: .color(accent.opacity(0.16))) }
        if isDisplay {
            // A bar down the left edge, drawn inside the card.
            var clipped = context
            clipped.clip(to: shape)
            clipped.fill(
                Path(CGRect(x: card.minX, y: card.minY, width: max(4 * zoom, 2), height: card.height)),
                with: .color(colors.displayFlag)
            )
        }
        // A card whose node failed is outlined in red, selected or not: the selection shows in its
        // fill and in the badge, since a failure must not be hidden by being picked.
        let failed = node.error != nil
        let outline: Color = failed ? .red : (selected ? accent : cardStroke)
        context.stroke(shape, with: .color(outline), lineWidth: (selected || failed) ? 2.5 : 1)
        paintBadge(of: node, in: card, &context)

        paintPorts(of: node, type: type, &context)
        guard zoom >= 0.3 else { return }

        let left = card.minX + (isDisplay ? 12 : 9) * zoom
        let flagSpace: CGFloat = isDisplay ? 18 * zoom : 0
        let textWidth = card.width - (left - card.minX) - 8 * zoom - flagSpace
        let name = fitted(node.name, font: .system(size: 12 * zoom, weight: .semibold), width: textWidth, in: context)
        context.draw(
            context.resolve(Text(name).font(.system(size: 12 * zoom, weight: .semibold)).foregroundColor(colors.text)),
            at: CGPoint(x: left, y: card.minY + card.height * (zoom >= 0.5 ? 0.38 : 0.5)),
            anchor: .leading
        )
        if zoom >= 0.5 {
            let label = fitted(type.label, font: .system(size: 10 * zoom), width: textWidth, in: context)
            context.draw(
                context.resolve(Text(label).font(.system(size: 10 * zoom)).foregroundColor(colors.textSecondary)),
                at: CGPoint(x: left, y: card.minY + card.height * 0.7),
                anchor: .leading
            )
        }
        if isDisplay {
            var flag = context.resolve(Image(systemName: "flag.fill"))
            flag.shading = .color(colors.displayFlag)
            let side = 11 * zoom
            context.draw(
                flag,
                in: CGRect(x: card.maxX - 8 * zoom - side, y: card.minY + 6 * zoom, width: side, height: side)
            )
        }
    }

    /// A red dot with a mark for a node that failed, a yellow one for a warning, at the bottom
    /// right of the card where the display flag is not.
    private func paintBadge(of node: EditorNode, in card: CGRect, _ context: inout GraphicsContext) {
        guard zoom >= 0.3, node.error != nil || !node.warnings.isEmpty else { return }
        let failed = node.error != nil
        let radius = max(6.5 * zoom, 4)
        let centre = CGPoint(x: card.maxX - radius - 4 * zoom, y: card.maxY - radius - 3 * zoom)
        let circle = Path(ellipseIn: CGRect(x: centre.x - radius, y: centre.y - radius, width: radius * 2, height: radius * 2))
        context.fill(circle, with: .color(failed ? .red : .yellow))
        let mark = context.resolve(
            Text("!").font(.system(size: max(radius * 1.35, 7), weight: .heavy)).foregroundColor(failed ? .white : .black)
        )
        context.draw(mark, at: centre)
    }

    /// The text cut short with an ellipsis when it does not fit.
    private func fitted(_ text: String, font: Font, width: CGFloat, in context: GraphicsContext) -> String {
        func measure(_ candidate: String) -> CGFloat {
            context.resolve(Text(candidate).font(font)).measure(in: CGSize(width: 10_000, height: 1_000)).width
        }
        guard width > 0, measure(text) > width else { return text }
        var characters = Array(text)
        while characters.count > 1, measure(String(characters) + "…") > width { characters.removeLast() }
        return String(characters) + "…"
    }

    private func paintPorts(of node: EditorNode, type: EditorNodeType, _ context: inout GraphicsContext) {
        let radius = max(EditorLayout.portRadius * zoom, 2.5)
        let linked = Set(graph.links(into: node.id).map(\.targetInput))
        let outputsLinked = Set(graph.links.filter { $0.sourceNode == node.id }.map(\.sourceOutput))

        for index in 0..<type.singleInputCount {
            let centre = transform.viewPoint(EditorLayout.inputPoint(of: node, type: type, index: index, multiCount: 0))
            paintPort(at: centre, radius: radius, filled: linked.contains(index), &context)
        }
        if let bar = EditorLayout.multiInputBar(of: node, type: type) {
            let rect = transform.viewRect(bar)
            let shape = Path(roundedRect: rect, cornerRadius: rect.height / 2)
            let single = type.singleInputCount
            let isLinked = linked.contains { $0 >= single }
            context.fill(shape, with: .color(isLinked ? portFill : cardFill))
            context.stroke(shape, with: .color(portFill), lineWidth: 1)
        }
        for index in 0..<max(type.outputCount, 0) {
            let centre = transform.viewPoint(EditorLayout.outputPoint(of: node, type: type, index: index))
            paintPort(at: centre, radius: radius, filled: outputsLinked.contains(index), &context)
        }
    }

    private func paintPort(at centre: CGPoint, radius: CGFloat, filled: Bool, _ context: inout GraphicsContext) {
        let circle = Path(ellipseIn: CGRect(x: centre.x - radius, y: centre.y - radius, width: radius * 2, height: radius * 2))
        context.fill(circle, with: .color(filled ? portFill : cardFill))
        context.stroke(circle, with: .color(portFill), lineWidth: 1)
    }

    // MARK: The gesture in progress

    private func paintGesture(_ context: inout GraphicsContext) {
        switch model.interaction {
        case .marquee(let start, let current, _):
            let rect = NodeEditorModel.rect(from: start, to: current)
            context.fill(Path(rect), with: .color(accent.opacity(0.1)))
            context.stroke(Path(rect), with: .color(accent.opacity(0.8)), lineWidth: 1)
        case .linking(let drag) where drag.moved:
            guard let source = graph.node(drag.sourceNode) else { return }
            let start = EditorLayout.outputPoint(of: source, type: graph.type(of: source), index: drag.sourceOutput)
            let end = drag.target?.point ?? drag.cursor
            context.stroke(curve(from: start, to: end), with: .color(accent), style: StrokeStyle(lineWidth: 2.2, lineCap: .round))
            if let target = drag.target { paintSnap(target, &context) }
        default:
            break
        }
    }

    /// A ring on the input a link would land on, with its label.
    private func paintSnap(_ target: EditorLayout.InputTarget, _ context: inout GraphicsContext) {
        let centre = transform.viewPoint(target.point)
        let radius = max(EditorLayout.portRadius * zoom, 2.5) + 3
        context.stroke(
            Path(ellipseIn: CGRect(x: centre.x - radius, y: centre.y - radius, width: radius * 2, height: radius * 2)),
            with: .color(accent),
            lineWidth: 2
        )
        guard !target.label.isEmpty else { return }
        let text = context.resolve(Text(target.label).font(.system(size: 11, weight: .medium)).foregroundColor(colors.onAccent))
        let measured = text.measure(in: CGSize(width: 400, height: 40))
        let chip = CGRect(x: centre.x - measured.width / 2 - 6, y: centre.y - radius - measured.height - 10, width: measured.width + 12, height: measured.height + 4)
        context.fill(Path(roundedRect: chip, cornerRadius: chip.height / 2), with: .color(accent))
        context.draw(text, at: CGPoint(x: chip.midX, y: chip.midY))
    }

    private func paintEmptyHint(_ context: inout GraphicsContext) {
        let hint = context.resolve(
            Text("Press Tab to add a node").font(.system(size: 14)).foregroundColor(colors.textSecondary)
        )
        context.draw(hint, at: CGPoint(x: size.width / 2, y: size.height / 2))
    }
}
