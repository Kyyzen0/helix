import XCTest

final class ViewTransformTests: XCTestCase {
    func testRoundTrip() {
        let t = ViewTransform(offset: CGPoint(x: 30, y: -20), zoom: 2)
        let p = CGPoint(x: 11, y: 7)
        let back = t.canvasPoint(t.viewPoint(p))
        XCTAssertEqual(back.x, p.x, accuracy: 1e-9)
        XCTAssertEqual(back.y, p.y, accuracy: 1e-9)
    }

    func testZoomKeepsPointUnderCursor() {
        let t = ViewTransform(offset: CGPoint(x: 10, y: 20), zoom: 1)
        let cursor = CGPoint(x: 200, y: 150)
        let before = t.canvasPoint(cursor)
        let z = t.zoomed(by: 2, around: cursor)
        XCTAssertEqual(z.zoom, 2, accuracy: 1e-9)
        XCTAssertEqual(z.canvasPoint(cursor).x, before.x, accuracy: 1e-9)
        XCTAssertEqual(z.canvasPoint(cursor).y, before.y, accuracy: 1e-9)
    }

    func testZoomIsClamped() {
        let t = ViewTransform()
        XCTAssertEqual(t.zoomed(by: 1000, around: .zero).zoom, 5)
        XCTAssertEqual(t.zoomed(by: 0.0001, around: .zero).zoom, 0.1, accuracy: 1e-12)
        XCTAssertEqual(t.zoomed(by: -1, around: .zero), t)
    }

    func testFramingCentresAndFits() {
        let rect = CGRect(x: 0, y: 0, width: 1000, height: 400)
        let size = CGSize(width: 600, height: 400)
        let t = ViewTransform.framing(rect, in: size)
        let view = t.viewRect(rect)
        XCTAssertEqual(view.midX, 300, accuracy: 1e-6)
        XCTAssertEqual(view.midY, 200, accuracy: 1e-6)
        XCTAssertLessThanOrEqual(view.width, 600)
        // A single small card is not blown up.
        XCTAssertLessThanOrEqual(ViewTransform.framing(CGRect(x: 0, y: 0, width: 10, height: 10), in: size).zoom, 1.5)
    }
}

final class EditorLayoutTests: XCTestCase {
    let boolean = EditorNodeType(name: "helix::boolean", label: "Boolean",
                                 inputs: [EditorPortInfo(label: "A"), EditorPortInfo(label: "B")])
    let merge = EditorNodeType(name: "helix::merge", label: "Merge",
                               inputs: [EditorPortInfo(label: "Geometry", multiInput: true)])
    let cube = EditorNodeType(name: "helix::cube", label: "Cube")

    func graph(_ nodes: [EditorNode], _ links: [EditorLink] = []) -> EditorGraph {
        EditorGraph(nodes: nodes, links: links, displayNode: nil,
                    types: [boolean.name: boolean, merge.name: merge, cube.name: cube])
    }

    func node(_ id: UInt64, _ type: EditorNodeType, _ x: CGFloat, _ y: CGFloat) -> EditorNode {
        EditorNode(id: id, type: type.name, name: "n\(id)", path: "/n\(id)", position: CGPoint(x: x, y: y))
    }

    func testPortsFollowTheQtGeometry() {
        let n = node(1, boolean, 100, 50)
        // Card 148 wide centred on x = 100: left 26. Two ports: width * (i + 1) / 3.
        XCTAssertEqual(EditorLayout.inputPoint(of: n, type: boolean, index: 0, multiCount: 0).x, 26 + 148.0 / 3, accuracy: 1e-9)
        XCTAssertEqual(EditorLayout.inputPoint(of: n, type: boolean, index: 1, multiCount: 0).x, 26 + 148.0 * 2 / 3, accuracy: 1e-9)
        XCTAssertEqual(EditorLayout.inputPoint(of: n, type: boolean, index: 0, multiCount: 0).y, 50 - 23)
        XCTAssertEqual(EditorLayout.outputPoint(of: n, type: boolean, index: 0), CGPoint(x: 100, y: 73))
    }

    func testMultiInputLinksSpreadAlongTheBar() {
        let n = node(1, merge, 0, 0)
        let a = EditorLayout.inputPoint(of: n, type: merge, index: 0, multiCount: 3)
        let b = EditorLayout.inputPoint(of: n, type: merge, index: 1, multiCount: 3)
        let c = EditorLayout.inputPoint(of: n, type: merge, index: 2, multiCount: 3)
        XCTAssertLessThan(a.x, b.x)
        XCTAssertLessThan(b.x, c.x)
        XCTAssertEqual(b.x, 0, accuracy: 1e-9)
    }

    func testNextMultiInput() {
        let m = node(2, merge, 0, 100)
        var g = graph([node(1, cube, 0, 0), m])
        XCTAssertEqual(g.nextMultiInput(of: m), 0)
        g = graph([node(1, cube, 0, 0), m], [EditorLink(sourceNode: 1, sourceOutput: 0, targetNode: 2, targetInput: 0),
                                           EditorLink(sourceNode: 1, sourceOutput: 0, targetNode: 2, targetInput: 1)])
        XCTAssertEqual(g.nextMultiInput(of: m), 2)
    }

    func testLinksAreRemovedHighestInputFirst() {
        func link(_ target: UInt64, _ input: Int) -> EditorLink {
            EditorLink(sourceNode: 9, sourceOutput: 0, targetNode: target, targetInput: input)
        }
        let selected: Set<EditorLink> = [link(2, 0), link(2, 2), link(2, 1), link(3, 0), link(3, 1)]

        // The engine closes the gap of a multi-input at once, as an array does when an element
        // goes. Each input holds the index it had when the links were selected, so every removal
        // must still find that link at that index.
        var inputs: [UInt64: [Int]] = [2: [0, 1, 2, 3], 3: [0, 1]]
        for link in EditorLink.removalOrder(selected) {
            let wired = inputs[link.targetNode] ?? []
            XCTAssertTrue(link.targetInput < wired.count && wired[link.targetInput] == link.targetInput,
                          "input \(link.targetInput) of node \(link.targetNode) moved before it was removed")
            if link.targetInput < wired.count { inputs[link.targetNode]?.remove(at: link.targetInput) }
        }
        XCTAssertEqual(inputs[2], [3])
        XCTAssertEqual(inputs[3], [])
    }

    func testHitTestingPrefersPortsThenCardsThenLinks() {
        let a = node(1, cube, 0, 0), b = node(2, boolean, 0, 120)
        let link = EditorLink(sourceNode: 1, sourceOutput: 0, targetNode: 2, targetInput: 0)
        let g = graph([a, b], [link])
        XCTAssertEqual(EditorLayout.hit(at: CGPoint(x: 0, y: 23), zoom: 1, in: g), .output(node: 1, index: 0))
        XCTAssertEqual(EditorLayout.hit(at: CGPoint(x: 10, y: 5), zoom: 1, in: g), .node(1))
        let end = EditorLayout.inputPoint(of: b, type: boolean, index: 0, multiCount: 0)
        XCTAssertEqual(EditorLayout.hit(at: end, zoom: 1, in: g), .linkedInput(link))
        XCTAssertEqual(EditorLayout.hit(at: CGPoint(x: 100, y: 500), zoom: 1, in: g), .none)
        // The middle of the link, away from both cards.
        let mid = CGPoint(x: (a.position.x + end.x) / 2 + 0.0, y: 70)
        if case .link = EditorLayout.hit(at: CGPoint(x: EditorLayout.curvePoints(from: CGPoint(x: 0, y: 23), to: end)[12].x,
                                                     y: EditorLayout.curvePoints(from: CGPoint(x: 0, y: 23), to: end)[12].y), zoom: 1, in: g) {} else {
            XCTFail("a point on the link should hit it \(mid)")
        }
    }

    func testPortGrabKeepsItsScreenSize() {
        let g = graph([node(1, cube, 0, 0)])
        // 12 canvas units from the port: out of reach at zoom 1, in reach at zoom 0.5.
        XCTAssertNotEqual(EditorLayout.hit(at: CGPoint(x: 0, y: 35), zoom: 1, in: g), .output(node: 1, index: 0))
        XCTAssertEqual(EditorLayout.hit(at: CGPoint(x: 0, y: 35), zoom: 0.5, in: g), .output(node: 1, index: 0))
    }

    func testSnapPicksNearestInputAndSkipsTheSource() {
        let src = node(1, cube, 0, 0), target = node(2, boolean, 0, 120), far = node(3, boolean, 600, 120)
        let g = graph([src, target, far])
        let b = EditorLayout.inputPoint(of: target, type: boolean, index: 1, multiCount: 0)
        let snap = EditorLayout.snapTarget(near: CGPoint(x: b.x + 3, y: b.y - 4), zoom: 1, in: g, excluding: 1)
        XCTAssertEqual(snap?.node, 2)
        XCTAssertEqual(snap?.input, 1)
        XCTAssertNil(EditorLayout.snapTarget(near: CGPoint(x: 300, y: 300), zoom: 1, in: g, excluding: 1))
        XCTAssertNil(EditorLayout.snapTarget(near: CGPoint(x: 0, y: 0), zoom: 1, in: g, excluding: 1))
    }

    func testSnapOnMultiInputAppends() {
        let src = node(1, cube, 0, 0), m = node(2, merge, 0, 120)
        let g = graph([src, m], [EditorLink(sourceNode: 1, sourceOutput: 0, targetNode: 2, targetInput: 0)])
        XCTAssertEqual(EditorLayout.snapTarget(near: CGPoint(x: 0, y: 100), zoom: 1, in: g, excluding: 1)?.input, 1)
    }

    func testMarqueeTouchesCards() {
        let g = graph([node(1, cube, 0, 0), node(2, cube, 500, 0)])
        XCTAssertEqual(EditorLayout.nodes(in: CGRect(x: -10, y: -10, width: 30, height: 30), graph: g), [1])
        XCTAssertEqual(EditorLayout.nodes(in: CGRect(x: -100, y: -100, width: 1000, height: 200), graph: g), [1, 2])
        XCTAssertTrue(EditorLayout.nodes(in: CGRect(x: 200, y: 200, width: 10, height: 10), graph: g).isEmpty)
    }

    func testPlacementChainsUnderTheSelectedNode() {
        let a = node(1, cube, 40, 10)
        let g = graph([a])
        let wired = NodePlacement.place(boolean, at: CGPoint(x: 999, y: 999), selection: [1], in: g)
        XCTAssertEqual(wired.position, CGPoint(x: 40, y: 130))
        XCTAssertEqual(wired.wiredFrom?.node, 1)
        // A type with no input goes under the pointer, wired to nothing.
        let free = NodePlacement.place(cube, at: CGPoint(x: 300, y: 300), selection: [1], in: g)
        XCTAssertEqual(free.position, CGPoint(x: 300, y: 300))
        XCTAssertNil(free.wiredFrom)
        // Two selected nodes: no chaining.
        XCTAssertNil(NodePlacement.place(boolean, at: .zero, selection: [1, 2], in: g).wiredFrom)
    }

    func testPlacementAvoidsAnOccupiedSpot() {
        let g = graph([node(1, cube, 0, 0), node(2, boolean, 0, 120)])
        let placed = NodePlacement.place(boolean, at: .zero, selection: [1], in: g)
        XCTAssertEqual(placed.position.y, 120)
        XCTAssertGreaterThan(placed.position.x, 100)
    }
}

final class NodeSearchTests: XCTestCase {
    let types = [
        EditorNodeType(name: "helix::cube", label: "Cube", tags: ["primitive", "box"]),
        EditorNodeType(name: "helix::cylinder", label: "Cylinder", tags: ["primitive"]),
        EditorNodeType(name: "helix::attributeNoise", label: "Attribute Noise", tags: ["noise"]),
        EditorNodeType(name: "helix::copyToPoints", label: "Copy To Points", tags: ["copy"]),
        EditorNodeType(name: "helix::merge", label: "Merge", tags: ["combine"]),
    ]

    func names(_ q: String) -> [String] { NodeSearch.rank(q, in: types).map(\.type.label) }

    func testEmptyQueryListsEverythingByLabel() {
        XCTAssertEqual(names(""), ["Attribute Noise", "Copy To Points", "Cube", "Cylinder", "Merge"])
    }

    func testPrefixBeatsSubsequence() {
        XCTAssertEqual(names("cu").first, "Cube")
        XCTAssertTrue(names("cu").contains("Cylinder") == false)
        XCTAssertEqual(names("cyl"), ["Cylinder"])
    }

    func testSubsequenceAndWordPrefix() {
        XCTAssertEqual(names("attrn"), ["Attribute Noise"])
        XCTAssertEqual(names("noise").first, "Attribute Noise")
        XCTAssertEqual(names("copyto").first, "Copy To Points")
        XCTAssertEqual(names("pts").first, "Copy To Points")
    }

    func testTagsMatchAloneButBelowLabels() {
        XCTAssertEqual(Set(names("primitive")), ["Cube", "Cylinder"])
        XCTAssertEqual(names("combine"), ["Merge"])
        XCTAssertTrue(names("zzz").isEmpty)
    }

    func testShortNameMatchesWhenLabelDiffers() {
        let t = [EditorNodeType(name: "helix::geometryImport", label: "Import Geometry")]
        XCTAssertEqual(NodeSearch.rank("geometryimp", in: t).count, 1)
    }
}

@MainActor
final class NodeEditorModelTests: XCTestCase {
    final class Recorder: NodeEditorEditing {
        var calls: [String] = []
        var moves: [NodeMove] = []
        var error: String?
        func moveNodes(_ m: [NodeMove]) -> EditOutcome { calls.append("move"); moves = m; return EditOutcome(error: error) }
        func connect(_ l: EditorLink) -> EditOutcome { calls.append("connect \(l.sourceNode)>\(l.targetNode).\(l.targetInput)"); return EditOutcome(error: error) }
        func reconnect(_ o: EditorLink, to n: EditorLink) -> EditOutcome { calls.append("reconnect \(n.targetNode).\(n.targetInput)"); return .done }
        func disconnect(_ l: EditorLink) -> EditOutcome { calls.append("disconnect"); return .done }
        func delete(nodes: [UInt64], links: [EditorLink]) -> EditOutcome { calls.append("delete \(nodes) \(links.count)"); return .done }
        func create(_ t: EditorNodeType, at: CGPoint, wiredFrom: (node: UInt64, output: Int)?) -> EditOutcome {
            calls.append("create \(t.name) \(at.x),\(at.y) \(wiredFrom?.node ?? 0)"); return EditOutcome(error: nil, createdNode: 99)
        }
        func setDisplay(_ n: UInt64) -> EditOutcome { calls.append("display \(n)"); return .done }
    }

    let cube = EditorNodeType(name: "helix::cube", label: "Cube")
    let boolean = EditorNodeType(name: "helix::boolean", label: "Boolean", inputs: [EditorPortInfo(label: "A"), EditorPortInfo(label: "B")])

    func makeModel() -> (NodeEditorModel, Recorder) {
        let model = NodeEditorModel()
        let rec = Recorder()
        model.editing = rec
        let nodes = [
            EditorNode(id: 1, type: cube.name, name: "a", path: "/a", position: CGPoint(x: 0, y: 0)),
            EditorNode(id: 2, type: boolean.name, name: "b", path: "/b", position: CGPoint(x: 0, y: 120)),
            EditorNode(id: 3, type: cube.name, name: "c", path: "/c", position: CGPoint(x: 400, y: 0)),
        ]
        model.reload(EditorGraph(nodes: nodes, links: [], displayNode: nil, types: [cube.name: cube, boolean.name: boolean]))
        model.setViewSize(CGSize(width: 800, height: 600))
        model.zoom(by: 1, around: .zero)
        return (model, rec)
    }

    /// The view point of a canvas point at the identity transform.
    func v(_ model: NodeEditorModel, _ x: CGFloat, _ y: CGFloat) -> CGPoint { model.transform.viewPoint(CGPoint(x: x, y: y)) }

    func testClickSelectsAndShiftClickToggles() {
        let (m, _) = makeModel()
        m.mouseDown(at: v(m, 10, 0), clickCount: 1, modifiers: []); m.mouseUp(at: v(m, 10, 0))
        XCTAssertEqual(m.selectedNodes, [1])
        m.mouseDown(at: v(m, 400, 0), clickCount: 1, modifiers: .shift); m.mouseUp(at: v(m, 400, 0))
        XCTAssertEqual(m.selectedNodes, [1, 3])
        m.mouseDown(at: v(m, 400, 0), clickCount: 1, modifiers: .command); m.mouseUp(at: v(m, 400, 0))
        XCTAssertEqual(m.selectedNodes, [1])
        m.mouseDown(at: v(m, 700, 500), clickCount: 1, modifiers: []); m.mouseUp(at: v(m, 700, 500))
        XCTAssertTrue(m.selectedNodes.isEmpty)
    }

    func testMarqueeSelects() {
        let (m, _) = makeModel()
        m.mouseDown(at: v(m, -100, -100), clickCount: 1, modifiers: [])
        m.mouseDragged(to: v(m, 100, 200))
        m.mouseUp(at: v(m, 100, 200))
        XCTAssertEqual(m.selectedNodes, [1, 2])
    }

    func testDragMovesSelectionOnceAtTheEnd() {
        let (m, rec) = makeModel()
        let start = v(m, 10, 0)
        m.mouseDown(at: start, clickCount: 1, modifiers: [])
        m.mouseDragged(to: CGPoint(x: start.x + 30, y: start.y + 10))
        XCTAssertTrue(rec.calls.isEmpty, "nothing reaches the engine while dragging")
        XCTAssertEqual(m.displayPosition(of: m.graph.node(1)!).x, 30 / m.transform.zoom, accuracy: 1e-6)
        m.mouseUp(at: CGPoint(x: start.x + 30, y: start.y + 10))
        XCTAssertEqual(rec.calls, ["move"])
        XCTAssertEqual(rec.moves.count, 1)
    }

    func testClickWithoutMovingMakesNoEdit() {
        let (m, rec) = makeModel()
        m.mouseDown(at: v(m, 10, 0), clickCount: 1, modifiers: []); m.mouseUp(at: v(m, 10, 0))
        XCTAssertTrue(rec.calls.isEmpty)
    }

    func testDragFromOutputToInputConnects() {
        let (m, rec) = makeModel()
        let out = v(m, 0, 23)
        m.mouseDown(at: out, clickCount: 1, modifiers: [])
        let target = EditorLayout.inputPoint(of: m.graph.node(2)!, type: boolean, index: 1, multiCount: 0)
        m.mouseDragged(to: m.transform.viewPoint(target))
        m.mouseUp(at: m.transform.viewPoint(target))
        XCTAssertEqual(rec.calls, ["connect 1>2.1"])
    }

    func testDropOnNothingFromAFreshLinkDoesNothing() {
        let (m, rec) = makeModel()
        m.mouseDown(at: v(m, 0, 23), clickCount: 1, modifiers: [])
        m.mouseDragged(to: v(m, 300, 300)); m.mouseUp(at: v(m, 300, 300))
        XCTAssertTrue(rec.calls.isEmpty)
    }

    func testPullingALinkOffAndDroppingInTheVoidDisconnects() {
        let (m, rec) = makeModel()
        let link = EditorLink(sourceNode: 1, sourceOutput: 0, targetNode: 2, targetInput: 0)
        m.reload(EditorGraph(nodes: m.graph.nodes, links: [link], displayNode: nil, types: m.graph.types))
        let end = m.transform.viewPoint(EditorLayout.inputPoint(of: m.graph.node(2)!, type: boolean, index: 0, multiCount: 0))
        m.mouseDown(at: end, clickCount: 1, modifiers: [])
        m.mouseDragged(to: CGPoint(x: end.x + 200, y: end.y + 200)); m.mouseUp(at: CGPoint(x: end.x + 200, y: end.y + 200))
        XCTAssertEqual(rec.calls, ["disconnect"])
    }

    func testPullingALinkOffToAnotherInputReconnects() {
        let (m, rec) = makeModel()
        let link = EditorLink(sourceNode: 1, sourceOutput: 0, targetNode: 2, targetInput: 0)
        m.reload(EditorGraph(nodes: m.graph.nodes, links: [link], displayNode: nil, types: m.graph.types))
        let end = m.transform.viewPoint(EditorLayout.inputPoint(of: m.graph.node(2)!, type: boolean, index: 0, multiCount: 0))
        let other = m.transform.viewPoint(EditorLayout.inputPoint(of: m.graph.node(2)!, type: boolean, index: 1, multiCount: 0))
        m.mouseDown(at: end, clickCount: 1, modifiers: [])
        m.mouseDragged(to: other); m.mouseUp(at: other)
        XCTAssertEqual(rec.calls, ["reconnect 2.1"])
    }

    func testDoubleClickSetsDisplayAndDeleteRemovesSelection() {
        let (m, rec) = makeModel()
        m.mouseDown(at: v(m, 400, 0), clickCount: 2, modifiers: [])
        m.mouseUp(at: v(m, 400, 0))
        XCTAssertEqual(rec.calls, ["display 3"])
        m.selectAll()
        m.deleteSelection()
        XCTAssertEqual(rec.calls.last, "delete [1, 2, 3] 0")
    }

    func testRefusalShowsAMessage() {
        let (m, rec) = makeModel()
        rec.error = "The link would make the node depend on itself."
        m.mouseDown(at: v(m, 0, 23), clickCount: 1, modifiers: [])
        let target = m.transform.viewPoint(EditorLayout.inputPoint(of: m.graph.node(2)!, type: boolean, index: 0, multiCount: 0))
        m.mouseDragged(to: target); m.mouseUp(at: target)
        XCTAssertEqual(m.message, rec.error)
    }

    func testAddNodeSelectsTheNewOneAndChainsFromSelection() {
        let (m, rec) = makeModel()
        m.select(nodes: [3])
        m.openPalette(at: CGPoint(x: 100, y: 100))
        m.addNode(boolean)
        XCTAssertEqual(rec.calls.last, "create helix::boolean 400.0,120.0 3")
        XCTAssertNil(m.palette)
    }

    func testFrameAllFitsEveryNode() {
        let (m, _) = makeModel()
        m.frameAll()
        for n in m.graph.nodes {
            let r = m.transform.viewRect(EditorLayout.cardRect(at: n.position))
            XCTAssertTrue(CGRect(x: 0, y: 0, width: 800, height: 600).contains(r))
        }
    }
}
