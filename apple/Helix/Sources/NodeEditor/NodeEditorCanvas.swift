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
        paintHover(&context)
        paintGesture(&context)
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
        // Red is for errors alone. A card whose node failed gets a thick red outline and the
        // triangle below, selected or not: the selection shows in its fill and in an accent ring
        // inside the outline, since a failure must not be hidden by being picked.
        let failed = node.error != nil
        if failed {
            context.stroke(shape, with: .color(colors.error), lineWidth: 3.5)
            if selected, card.width > 14 {
                context.stroke(Path(roundedRect: card.insetBy(dx: 3.5, dy: 3.5), cornerRadius: max(radius - 3, 1)), with: .color(accent), lineWidth: 1.5)
            }
        } else {
            context.stroke(shape, with: .color(selected ? accent : cardStroke), lineWidth: selected ? 2.5 : 1)
        }
        paintBadge(of: node, in: card, &context)

        paintPorts(of: node, type: type, &context)
        paintEye(of: node, in: card, shape: shape, &context)

        // The name stays down to the smallest zoom, in a font that does not shrink below what
        // can still be told apart. The type under it joins it from 0.4.
        let hasEye = zoom >= EditorLayout.eyeMinZoom
        let left = card.minX + (hasEye ? 27 : (isDisplay ? 12 : 9)) * zoom
        let textWidth = card.width - (left - card.minX) - 8 * zoom
        let twoLines = zoom >= 0.4
        let nameFont = Font.system(size: max(13 * zoom, 7), weight: .semibold)
        let name = fitted(node.name, font: nameFont, width: textWidth, in: context)
        context.draw(
            context.resolve(Text(name).font(nameFont).foregroundColor(colors.text)),
            at: CGPoint(x: left, y: card.minY + card.height * (zoom >= 0.5 ? 0.38 : (twoLines ? 0.34 : 0.5))),
            anchor: .leading
        )
        if twoLines {
            let labelFont = Font.system(size: max(11 * zoom, 6))
            let label = fitted(graph.label(of: node), font: labelFont, width: textWidth, in: context)
            context.draw(
                context.resolve(Text(label).font(labelFont).foregroundColor(colors.textSecondary)),
                at: CGPoint(x: left, y: card.minY + card.height * (zoom >= 0.5 ? 0.7 : 0.72)),
                anchor: .leading
            )
        }
    }

    /// The eye at the left of a card: filled, in the colour of the display, on the node that is
    /// shown in the 3D view, and an outline on a card the pointer is over, which the person can
    /// click to show it. It lights up when the pointer is on the eye itself.
    private func paintEye(of node: EditorNode, in card: CGRect, shape: Path, _ context: inout GraphicsContext) {
        guard zoom >= EditorLayout.eyeMinZoom else { return }
        let isDisplay = graph.displayNode == node.id
        let overCard = model.hover.node == node.id
        let overEye = overCard && model.hover.eye
        guard isDisplay || overCard else { return }
        let strip = CGRect(x: card.minX, y: card.minY, width: EditorLayout.eyeWidth * zoom, height: card.height)
        if overEye {
            var clipped = context
            clipped.clip(to: shape)
            clipped.fill(Path(strip), with: .color(colors.text.opacity(0.1)))
        }
        let name = isDisplay ? "eye.fill" : "eye"
        var symbol = context.resolve(Image(systemName: name))
        symbol.shading = .color(isDisplay ? colors.displayFlag : (overEye ? colors.text : colors.textSecondary))
        let width = 15 * zoom
        context.draw(symbol, in: CGRect(x: strip.midX - width / 2 + 1 * zoom, y: strip.midY - width / 2, width: width, height: width))
    }

    /// A red triangle for a node that failed, an orange circle for one that only warns, at the
    /// bottom right of the card where the display flag is not. Drawn at every zoom, at a size
    /// that does not shrink with it below what reads: a failure must not disappear when the
    /// graph is zoomed out to see all of it.
    private func paintBadge(of node: EditorNode, in card: CGRect, _ context: inout GraphicsContext) {
        guard node.error != nil || !node.warnings.isEmpty else { return }
        let failed = node.error != nil
        let side = max(15 * zoom, 13)
        let rect = CGRect(x: card.maxX - side - 3 * zoom, y: card.maxY - side - 2 * zoom, width: side, height: side)
        // A card-coloured disc behind it keeps the glyph readable over the outline and the ports.
        context.fill(Path(ellipseIn: rect.insetBy(dx: -1.5, dy: -1.5)), with: .color(cardFill))
        var symbol = context.resolve(Image(systemName: failed ? "exclamationmark.triangle.fill" : "exclamationmark.circle.fill"))
        symbol.shading = .color(failed ? colors.error : Color.orange)
        context.draw(symbol, in: rect)
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
        // A port the pointer is on grows a little, so it reads as the thing under it.
        func grown(_ port: EditorPort) -> CGFloat { model.hover.port == port ? radius * 1.45 : radius }

        for index in 0..<type.singleInputCount {
            let centre = transform.viewPoint(EditorLayout.inputPoint(of: node, type: type, index: index, multiCount: 0))
            paintPort(at: centre, radius: grown(.input(node: node.id, index: index)), filled: linked.contains(index), &context)
        }
        if let bar = EditorLayout.multiInputBar(of: node, type: type) {
            var rect = transform.viewRect(bar)
            if model.hover.port == .input(node: node.id, index: type.inputs.count - 1) { rect = rect.insetBy(dx: -2, dy: -1.5) }
            let shape = Path(roundedRect: rect, cornerRadius: rect.height / 2)
            let single = type.singleInputCount
            let isLinked = linked.contains { $0 >= single }
            context.fill(shape, with: .color(isLinked ? portFill : cardFill))
            context.stroke(shape, with: .color(portFill), lineWidth: 1)
        }
        for index in 0..<max(type.outputCount, 0) {
            let centre = transform.viewPoint(EditorLayout.outputPoint(of: node, type: type, index: index))
            paintPort(at: centre, radius: grown(.output(node: node.id, index: index)), filled: outputsLinked.contains(index), &context)
        }
        paintPortNames(of: node, type: type, &context)
    }

    /// The short names of the ports of a node that has several, above its inputs and under its
    /// outputs, once the zoom is large enough to read them. With one port the name says nothing
    /// the card does not, and it shows on hover instead.
    private func paintPortNames(of node: EditorNode, type: EditorNodeType, _ context: inout GraphicsContext) {
        guard zoom >= 1 else { return }
        let font = Font.system(size: 10)
        if type.inputs.count >= 2 {
            let slot = EditorLayout.cardSize.width / CGFloat(type.inputs.count + 1) * zoom
            for (index, port) in type.inputs.enumerated() where !port.label.isEmpty && model.hover.port != .input(node: node.id, index: index) {
                let anchor = index < type.singleInputCount
                    ? EditorLayout.inputPoint(of: node, type: type, index: index, multiCount: 0)
                    : EditorLayout.multiInputBar(of: node, type: type).map { CGPoint(x: $0.midX, y: $0.minY + 3) }
                guard let anchor else { continue }
                let centre = transform.viewPoint(anchor)
                let text = fitted(port.label, font: font, width: slot, in: context)
                context.draw(
                    context.resolve(Text(text).font(font).foregroundColor(colors.textSecondary)),
                    at: CGPoint(x: centre.x, y: centre.y - 8 - 3 * zoom)
                )
            }
        }
        if type.outputCount >= 2 {
            let slot = EditorLayout.cardSize.width / CGFloat(type.outputCount + 1) * zoom - 6
            for index in 0..<type.outputCount where model.hover.port != .output(node: node.id, index: index) {
                let centre = transform.viewPoint(EditorLayout.outputPoint(of: node, type: type, index: index))
                let text = fitted(EditorLayout.outputLabel(index, of: type.outputCount), font: font, width: slot, in: context)
                context.draw(
                    context.resolve(Text(text).font(font).foregroundColor(colors.textSecondary)),
                    at: CGPoint(x: centre.x, y: centre.y + 8 + 3 * zoom)
                )
            }
        }
    }

    private func paintPort(at centre: CGPoint, radius: CGFloat, filled: Bool, _ context: inout GraphicsContext) {
        let circle = Path(ellipseIn: CGRect(x: centre.x - radius, y: centre.y - radius, width: radius * 2, height: radius * 2))
        context.fill(circle, with: .color(filled ? portFill : cardFill))
        context.stroke(circle, with: .color(portFill), lineWidth: 1)
    }

    // MARK: Hover

    /// The label of the port the pointer is on, in a bubble over its card: above an input, under an
    /// output, drawn last so no card covers it.
    private func paintHover(_ context: inout GraphicsContext) {
        guard let port = model.hover.port, let point = EditorLayout.point(of: port, in: graph) else { return }
        let text = EditorLayout.label(of: port, in: graph)
        guard !text.isEmpty else { return }
        var shownPoint = point
        if let node = graph.node(port.node) {
            // The port moves with its card while the card is dragged.
            let delta = CGPoint(x: model.displayPosition(of: node).x - node.position.x, y: model.displayPosition(of: node).y - node.position.y)
            shownPoint = CGPoint(x: point.x + delta.x, y: point.y + delta.y)
        }
        let centre = transform.viewPoint(shownPoint)
        let resolved = context.resolve(Text(text).font(.system(size: 12, weight: .medium)).foregroundColor(colors.text))
        let measured = resolved.measure(in: CGSize(width: 400, height: 40))
        let above: Bool = { if case .input = port { return true } else { return false } }()
        let lift = max(EditorLayout.portRadius * zoom, 2.5) * 1.45 + 6
        let width = measured.width + 14, height = measured.height + 6
        // The wire of the port leaves towards one side, so the bubble goes to the other: it names
        // the port without covering the link it is about.
        var wireX: CGFloat?
        switch port {
        case .input(let id, let index):
            if let link = graph.link(into: id, input: index), let (start, _) = EditorLayout.endpoints(of: link, in: graph) { wireX = start.x }
        case .output(let id, let index):
            let link = graph.links.first { $0.sourceNode == id && $0.sourceOutput == index }
            if let link, let (_, end) = EditorLayout.endpoints(of: link, in: graph) { wireX = end.x }
        }
        var chip = CGRect(x: centre.x - width / 2, y: above ? centre.y - lift - height : centre.y + lift, width: width, height: height)
        if let wireX {
            let wireInView = transform.viewPoint(CGPoint(x: wireX, y: 0)).x
            chip.origin.x = wireInView > centre.x ? centre.x - 10 - width : centre.x + 10
            // Above the row of the short names of the other ports, which it would cover.
            chip.origin.y = above ? centre.y - 20 - height : centre.y + 20
        }
        // Kept inside the view, so a card at an edge does not cut it off.
        chip.origin.x = min(max(chip.origin.x, 4), max(size.width - width - 4, 4))
        chip.origin.y = min(max(chip.origin.y, 4), max(size.height - height - 4, 4))
        let bubble = Path(roundedRect: chip, cornerRadius: height / 2)
        context.fill(bubble, with: .color(cardFill))
        context.stroke(bubble, with: .color(accent), lineWidth: 1.2)
        context.draw(resolved, at: CGPoint(x: chip.midX, y: chip.midY))
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
        let text = context.resolve(Text(target.label).font(.system(size: 12, weight: .medium)).foregroundColor(colors.onAccent))
        let measured = text.measure(in: CGSize(width: 400, height: 40))
        let chip = CGRect(x: centre.x - measured.width / 2 - 6, y: centre.y - radius - measured.height - 10, width: measured.width + 12, height: measured.height + 4)
        context.fill(Path(roundedRect: chip, cornerRadius: chip.height / 2), with: .color(accent))
        context.draw(text, at: CGPoint(x: chip.midX, y: chip.midY))
    }
}
