import Combine
import CoreGraphics
import Foundation

/// What an edit of the graph came to.
struct EditOutcome {
    /// Why the engine refused, in words for a person, or nil when it did what was asked.
    var error: String?
    /// The node an edit made, when it made one.
    var createdNode: UInt64?
    /// True when the edit was not made yet: the person is being asked first (see
    /// `GeometryLimits`), and it is made, or not, from the answer. It is no failure.
    var deferred = false

    static let done = EditOutcome()
    static func failed(_ message: String) -> EditOutcome { EditOutcome(error: message) }
    static let waiting = EditOutcome(deferred: true)
}

struct NodeMove: Equatable {
    var id: UInt64
    var position: CGPoint
}

/// What the editor asks of the document. Each call is one step of undo, and the document gives
/// the editor the graph back, through `NodeEditorModel.reload`, before the call returns.
@MainActor
protocol NodeEditorEditing: AnyObject {
    func moveNodes(_ moves: [NodeMove]) -> EditOutcome
    func connect(_ link: EditorLink) -> EditOutcome
    /// Takes a link off an input and puts it on another, as one step.
    func reconnect(_ old: EditorLink, to new: EditorLink) -> EditOutcome
    func disconnect(_ link: EditorLink) -> EditOutcome
    func delete(nodes: [UInt64], links: [EditorLink]) -> EditOutcome
    func create(_ type: EditorNodeType, at position: CGPoint, wiredFrom: (node: UInt64, output: Int)?) -> EditOutcome
    func setDisplay(_ node: UInt64) -> EditOutcome
}

/// Which keys are held with a click that extends the selection.
struct EditorModifiers: OptionSet, Sendable {
    let rawValue: Int
    static let shift = EditorModifiers(rawValue: 1)
    static let command = EditorModifiers(rawValue: 2)

    /// Shift and command both toggle a node in the selection instead of replacing it.
    var extendsSelection: Bool { !isDisjoint(with: [.shift, .command]) }
}

/// A link being dragged: from an output to the pointer, or to the input it would land on.
struct LinkDrag: Equatable {
    var sourceNode: UInt64
    var sourceOutput: Int
    /// The pointer, on the canvas.
    var cursor: CGPoint
    /// Where the button went down, in the view.
    var pressPoint: CGPoint
    var target: EditorLayout.InputTarget?
    /// The link this drag took off an input, which lands again, moves or goes away on release.
    var detached: EditorLink?
    /// Whether the pointer moved far enough for this to be a drag and not a click.
    var moved = false
}

/// Where the node palette opens.
struct PaletteRequest: Equatable {
    /// In the view, where the panel goes.
    var anchor: CGPoint
    /// On the canvas, where the new node goes when it is not chained to the selected one.
    var canvas: CGPoint
}

/// What the pointer was over when a context menu was asked for.
enum EditorContext: Equatable {
    case node(UInt64)
    case link(EditorLink)
    case canvas(CGPoint)
}

/// What the node editor shows and what a person does in it: the graph, the selection, the view
/// onto it, and the gesture in progress.
///
/// It holds no engine: the graph is a copy, and every edit goes through `editing`, which does
/// it and hands the new graph back. That keeps every rule of the editor (what a click selects,
/// where a link snaps, what a drop does) testable on its own.
@MainActor
final class NodeEditorModel: ObservableObject {
    enum Interaction: Equatable {
        case idle
        /// The button went down on a node and has not moved yet.
        case pressedNode(UInt64, start: CGPoint, extend: Bool, wasSelected: Bool)
        /// Selected nodes follow the pointer by `delta`, in canvas units. Nothing reaches the
        /// engine until the button goes up.
        case movingNodes(start: CGPoint, delta: CGPoint)
        case marquee(start: CGPoint, current: CGPoint, base: Set<UInt64>)
        case linking(LinkDrag)
        case panning(last: CGPoint)
    }

    static let dragThreshold: CGFloat = 3

    @Published private(set) var graph = EditorGraph()
    @Published private(set) var transform = ViewTransform()
    @Published private(set) var selectedNodes: Set<UInt64> = []
    @Published private(set) var selectedLinks: Set<EditorLink> = []
    @Published private(set) var interaction = Interaction.idle
    /// A refusal of the engine, shown for a few seconds over the editor.
    @Published private(set) var message: String?
    @Published private(set) var palette: PaletteRequest?
    /// Every type the palette offers, aliases included.
    @Published var paletteTypes: [EditorNodeType] = []

    weak var editing: NodeEditorEditing?
    /// The timeline of the window, which the space bar and the arrow keys drive while the editor
    /// has the keys.
    weak var timeline: TimelineModel?
    private(set) var viewSize = CGSize.zero
    /// The last place of the pointer in the view, for the palette to open at.
    var pointer: CGPoint?
    /// Whether the graph is to be framed as soon as the view has a size, as a window opens.
    private var framePending = false
    /// Whether the view keeps showing the whole graph as its size settles (a window opening, a
    /// pane being resized) until the person moves the view themselves.
    private var followsFraming = false
    /// How much the interface is scaled (Settings ▸ General), which the framing zooms in by so a
    /// small graph reads at that size.
    var interfaceScale: CGFloat = 1

    private var messageGeneration = 0

    nonisolated init() {}

    // MARK: Diagnostics

    /// The cards that carry an error or a warning, where they are in the view, with what to say
    /// when the pointer rests on one.
    func diagnosticAreas() -> [(rect: CGRect, text: String)] {
        graph.nodes.compactMap { node in
            guard let text = node.diagnosticText else { return nil }
            return (transform.viewRect(EditorLayout.cardRect(at: displayPosition(of: node))), text)
        }
    }

    // MARK: Graph

    /// Takes the graph the engine holds now. The selection keeps what is still there. A graph
    /// that was just opened is framed, once the view has a size to frame it in.
    func reload(_ graph: EditorGraph, frame: Bool = false) {
        self.graph = graph
        if frame {
            followsFraming = true
            if viewSize.width > 0, viewSize.height > 0 { frameAll() } else { framePending = true }
        }
        selectedNodes = selectedNodes.filter { graph.node($0) != nil }
        selectedLinks = selectedLinks.filter { graph.links.contains($0) }
        if case .linking(let drag) = interaction, graph.node(drag.sourceNode) == nil { interaction = .idle }
    }

    func select(nodes: Set<UInt64>) {
        selectedNodes = nodes.filter { graph.node($0) != nil }
        selectedLinks = []
    }

    func clearSelection() {
        selectedNodes = []
        selectedLinks = []
    }

    func selectAll() {
        selectedNodes = Set(graph.nodes.map(\.id))
        selectedLinks = []
    }

    /// Where a node is drawn: where the engine has it, and under the pointer while it is dragged.
    func displayPosition(of node: EditorNode) -> CGPoint {
        guard case .movingNodes(_, let delta) = interaction, selectedNodes.contains(node.id) else { return node.position }
        return CGPoint(x: node.position.x + delta.x, y: node.position.y + delta.y)
    }

    // MARK: Messages

    func report(_ text: String) {
        message = text
        messageGeneration += 1
        let generation = messageGeneration
        Task { @MainActor [weak self] in
            try? await Task.sleep(for: .seconds(4))
            if let self, self.messageGeneration == generation { self.message = nil }
        }
    }

    private func finish(_ outcome: EditOutcome) {
        if let error = outcome.error { report(error) }
    }

    // MARK: View

    func setViewSize(_ size: CGSize) {
        guard size != viewSize else { return }
        viewSize = size
        if (framePending || followsFraming), size.width > 0, size.height > 0 {
            framePending = false
            frameAll()
        }
    }

    /// Takes the interface size of Settings: a view that shows the whole graph frames it again,
    /// and one the person moved zooms by the change, so the cards read larger or smaller.
    func setInterfaceScale(_ scale: CGFloat) {
        guard scale != interfaceScale, scale > 0 else { return }
        let ratio = scale / interfaceScale
        interfaceScale = scale
        guard viewSize.width > 0, viewSize.height > 0 else { return }
        if followsFraming {
            frameAll()
        } else {
            transform = transform.zoomed(by: ratio, around: CGPoint(x: viewSize.width / 2, y: viewSize.height / 2))
        }
    }

    func pan(by delta: CGPoint) {
        followsFraming = false
        transform = transform.panned(by: delta)
    }

    func zoom(by factor: CGFloat, around point: CGPoint) {
        followsFraming = false
        transform = transform.zoomed(by: factor, around: point)
    }

    /// Shows every node, or the selected ones when there are some.
    func frameSelection() {
        frame(selectedNodes.isEmpty ? nil : selectedNodes)
    }

    func frameAll() {
        frame(nil)
    }

    private func frame(_ ids: Set<UInt64>?) {
        guard let bounds = graph.bounds(of: ids) ?? graph.bounds() else {
            transform = ViewTransform(offset: CGPoint(x: viewSize.width / 2, y: viewSize.height / 2), zoom: 1)
            return
        }
        transform = .framing(bounds, in: viewSize, maxFramedZoom: 1.5 * max(interfaceScale, 1))
    }

    /// Pans, if it has to, so the card of a node is in the view.
    func reveal(_ id: UInt64) {
        guard let node = graph.node(id), viewSize.width > 0 else { return }
        let card = transform.viewRect(EditorLayout.cardRect(at: node.position))
        let visible = CGRect(origin: .zero, size: viewSize).insetBy(dx: 16, dy: 16)
        if visible.contains(card) { return }
        let target = CGPoint(x: viewSize.width / 2, y: viewSize.height / 2)
        pan(by: CGPoint(x: target.x - card.midX, y: target.y - card.midY))
    }

    // MARK: Pointer

    func mouseDown(at point: CGPoint, clickCount: Int, modifiers: EditorModifiers) {
        pointer = point
        closePalette()
        let canvas = transform.canvasPoint(point)
        let extend = modifiers.extendsSelection

        switch EditorLayout.hit(at: canvas, zoom: transform.zoom, in: graph) {
        case .output(let node, let index):
            interaction = .linking(LinkDrag(
                sourceNode: node, sourceOutput: index, cursor: canvas, pressPoint: point
            ))
        case .linkedInput(let link):
            interaction = .linking(LinkDrag(
                sourceNode: link.sourceNode, sourceOutput: link.sourceOutput, cursor: canvas, pressPoint: point,
                detached: link
            ))
        case .node(let id):
            if clickCount >= 2, !extend {
                interaction = .idle
                finish(editing?.setDisplay(id) ?? .done)
                return
            }
            let wasSelected = selectedNodes.contains(id)
            selectedLinks = []
            if extend {
                if wasSelected { selectedNodes.remove(id) } else { selectedNodes.insert(id) }
            } else if !wasSelected {
                selectedNodes = [id]
            }
            interaction = .pressedNode(id, start: point, extend: extend, wasSelected: wasSelected)
        case .link(let link):
            selectedNodes = extend ? selectedNodes : []
            if extend, selectedLinks.contains(link) { selectedLinks.remove(link) } else {
                if !extend { selectedLinks = [] }
                selectedLinks.insert(link)
            }
            interaction = .idle
        case .none:
            let base = extend ? selectedNodes : []
            if !extend { clearSelection() }
            interaction = .marquee(start: point, current: point, base: base)
        }
    }

    func mouseDragged(to point: CGPoint) {
        pointer = point
        switch interaction {
        case .pressedNode(let id, let start, _, _):
            guard hypot(point.x - start.x, point.y - start.y) > Self.dragThreshold, selectedNodes.contains(id) else { return }
            interaction = .movingNodes(start: start, delta: canvasDelta(from: start, to: point))
        case .movingNodes(let start, _):
            interaction = .movingNodes(start: start, delta: canvasDelta(from: start, to: point))
        case .marquee(let start, _, let base):
            interaction = .marquee(start: start, current: point, base: base)
            let rect = transform.canvasRect(Self.rect(from: start, to: point))
            selectedNodes = base.union(EditorLayout.nodes(in: rect, graph: graph))
        case .linking(var drag):
            let canvas = transform.canvasPoint(point)
            drag.cursor = canvas
            if !drag.moved {
                drag.moved = hypot(point.x - drag.pressPoint.x, point.y - drag.pressPoint.y) > Self.dragThreshold
            }
            drag.target = EditorLayout.snapTarget(near: canvas, zoom: transform.zoom, in: graph, excluding: drag.sourceNode)
            interaction = .linking(drag)
        case .panning(let last):
            pan(by: CGPoint(x: point.x - last.x, y: point.y - last.y))
            interaction = .panning(last: point)
        case .idle:
            break
        }
    }

    func mouseUp(at point: CGPoint) {
        pointer = point
        let ended = interaction
        interaction = .idle
        switch ended {
        case .pressedNode(let id, _, let extend, let wasSelected):
            // A click on one of several selected nodes picks that one alone, once it is clear
            // it was not the start of a drag.
            if !extend, wasSelected, selectedNodes.count > 1 { selectedNodes = [id] }
        case .movingNodes(_, let delta):
            guard delta != .zero else { return }
            let moves = graph.nodes.filter { selectedNodes.contains($0.id) }.map {
                NodeMove(id: $0.id, position: CGPoint(x: $0.position.x + delta.x, y: $0.position.y + delta.y))
            }
            finish(editing?.moveNodes(moves) ?? .done)
        case .linking(let drag):
            finishLink(drag)
        case .panning, .marquee, .idle:
            break
        }
    }

    func mouseMoved(to point: CGPoint) {
        pointer = point
    }

    /// Starts a pan that follows the pointer, for the middle button.
    func beginPan(at point: CGPoint) {
        interaction = .panning(last: point)
    }

    private func finishLink(_ drag: LinkDrag) {
        // A press that did not move is a click: on the end of a link it selects the link.
        guard drag.moved else {
            if let link = drag.detached {
                selectedNodes = []
                selectedLinks = [link]
            }
            return
        }
        if let target = drag.target {
            let link = EditorLink(
                sourceNode: drag.sourceNode, sourceOutput: drag.sourceOutput,
                targetNode: target.node, targetInput: target.input
            )
            if let old = drag.detached {
                if old != link { finish(editing?.reconnect(old, to: link) ?? .done) }
            } else if !graph.links.contains(link) {
                finish(editing?.connect(link) ?? .done)
            }
        } else if let old = drag.detached {
            // Dropped on nothing: the link was taken off for good.
            finish(editing?.disconnect(old) ?? .done)
        }
    }

    private func canvasDelta(from start: CGPoint, to point: CGPoint) -> CGPoint {
        CGPoint(x: (point.x - start.x) / transform.zoom, y: (point.y - start.y) / transform.zoom)
    }

    static func rect(from a: CGPoint, to b: CGPoint) -> CGRect {
        CGRect(x: min(a.x, b.x), y: min(a.y, b.y), width: abs(a.x - b.x), height: abs(a.y - b.y))
    }

    // MARK: Commands

    /// Deletes the selected nodes, and the selected links the nodes do not take with them.
    func deleteSelection() {
        guard !selectedNodes.isEmpty || !selectedLinks.isEmpty else { return }
        let nodes = graph.nodes.map(\.id).filter { selectedNodes.contains($0) }
        let links = selectedLinks.filter { !selectedNodes.contains($0.sourceNode) && !selectedNodes.contains($0.targetNode) }
        finish(editing?.delete(nodes: nodes, links: Array(links)) ?? .done)
    }

    func setDisplay(_ id: UInt64) {
        finish(editing?.setDisplay(id) ?? .done)
    }

    // MARK: Context menu

    /// What a context menu at a point is about. A node or a link under the pointer is selected
    /// first, unless it already is, so the menu acts on what it shows.
    func context(at point: CGPoint) -> EditorContext {
        pointer = point
        let canvas = transform.canvasPoint(point)
        switch EditorLayout.hit(at: canvas, zoom: transform.zoom, in: graph) {
        case .node(let id), .output(let id, _):
            if !selectedNodes.contains(id) { select(nodes: [id]) }
            return .node(id)
        case .link(let link), .linkedInput(let link):
            selectedNodes = []
            selectedLinks = [link]
            return .link(link)
        case .none:
            return .canvas(canvas)
        }
    }

    // MARK: Palette

    /// Opens the palette at a point of the view, or at the pointer, or in the middle.
    func openPalette(at point: CGPoint? = nil) {
        let anchor = point ?? pointer ?? CGPoint(x: viewSize.width / 2, y: viewSize.height / 2)
        palette = PaletteRequest(anchor: anchor, canvas: transform.canvasPoint(anchor))
    }

    func closePalette() {
        palette = nil
    }

    /// Makes a node of a type, where the palette was opened or under the selected node, and
    /// selects it.
    ///
    /// A node made from a menu at a point of the view (`viewPoint`) goes there; one made from the
    /// shelf or the menu bar goes under the selected node, or in the middle of the view.
    func addNode(_ type: EditorNodeType, at viewPoint: CGPoint? = nil) {
        let canvas = viewPoint.map(transform.canvasPoint)
            ?? palette?.canvas
            ?? transform.canvasPoint(CGPoint(x: viewSize.width / 2, y: viewSize.height / 2))
        palette = nil
        let placement = NodePlacement.place(type, at: canvas, selection: selectedNodes, in: graph)
        let wasEmpty = graph.nodes.isEmpty
        let outcome = editing?.create(type, at: placement.position, wiredFrom: placement.wiredFrom) ?? .done
        finish(outcome)
        if let created = outcome.createdNode {
            select(nodes: [created])
            // The first node of a scene is framed, so it is where it can be read.
            if wasEmpty { frameAll() } else { reveal(created) }
        }
    }
}
