import XCTest

/// Naming a node, renaming it, and copying and pasting nodes: what the editor decides by itself,
/// with no engine.
final class NodeNamingTests: XCTestCase {
    func testTheNameComesFromTheLabelOfTheType() {
        XCTAssertEqual(NodeNaming.firstName(forLabel: "Grille"), "grille1")
        XCTAssertEqual(NodeNaming.firstName(forLabel: "Onde sinusoïdale"), "ondeSinusoidale1")
        XCTAssertEqual(NodeNaming.firstName(forLabel: "Bruit d’attribut"), "bruitAttribut1")
        XCTAssertEqual(NodeNaming.firstName(forLabel: "Bruit d'attribut"), "bruitAttribut1")
        XCTAssertEqual(NodeNaming.firstName(forLabel: "Montagne"), "montagne1")
        XCTAssertEqual(NodeNaming.firstName(forLabel: "Sine Wave"), "sineWave1")
        XCTAssertEqual(NodeNaming.firstName(forLabel: "Attribute Noise"), "attributeNoise1")
    }

    func testSmallWordsAndParticlesAreLeftOut() {
        XCTAssertEqual(NodeNaming.baseName(forLabel: "Copie sur des points"), "copieSurPoints")
        XCTAssertEqual(NodeNaming.baseName(forLabel: "Importation de géométrie"), "importationGeometrie")
        XCTAssertEqual(NodeNaming.baseName(forLabel: "Pose de squelette"), "poseSquelette")
        XCTAssertEqual(NodeNaming.baseName(forLabel: "Copy of the Points"), "copyPoints")
        XCTAssertEqual(NodeNaming.baseName(forLabel: "L’objet"), "objet")
        XCTAssertEqual(NodeNaming.baseName(forLabel: "Cell Fracture"), "cellFracture")
    }

    func testLigaturesAccentsAndSymbolsAreSpelledOutOrDropped() {
        XCTAssertEqual(NodeNaming.baseName(forLabel: "Cœur"), "coeur")
        XCTAssertEqual(NodeNaming.baseName(forLabel: "Étirement"), "etirement")
        XCTAssertEqual(NodeNaming.baseName(forLabel: "Géométrie 3D"), "geometrie3d")
        XCTAssertEqual(NodeNaming.baseName(forLabel: "Modèle : rat"), "modeleRat")
        XCTAssertEqual(NodeNaming.baseName(forLabel: "Skin-Deform"), "skinDeform")
    }

    func testALabelWithNothingUsableLeavesTheNameToTheEngine() {
        XCTAssertEqual(NodeNaming.baseName(forLabel: ""), "")
        XCTAssertEqual(NodeNaming.baseName(forLabel: "de la"), "")
        XCTAssertEqual(NodeNaming.baseName(forLabel: "!!! ???"), "")
        XCTAssertEqual(NodeNaming.firstName(forLabel: "日本語"), "")
        // Too long to take a number after it.
        XCTAssertEqual(NodeNaming.baseName(forLabel: String(repeating: "a", count: 70)), "")
        XCTAssertEqual(NodeNaming.firstName(forLabel: String(repeating: "a", count: 70)), "")
    }

    func testEveryNameMadeIsOneTheEngineTakes() {
        for label in ["Grille", "Onde sinusoïdale", "Bruit d’attribut", "Cœur", "Géométrie 3D", "Sine Wave", "Copy To Points", "3D Text"] {
            let name = NodeNaming.firstName(forLabel: label)
            XCTAssertTrue(NodeNaming.isValid(name), "\(label) → \(name)")
        }
    }

    func testWhatTheEngineTakesAsAName() {
        XCTAssertTrue(NodeNaming.isValid("grid_1"))
        XCTAssertTrue(NodeNaming.isValid("White_Knight"))
        XCTAssertTrue(NodeNaming.isValid(String(repeating: "a", count: NodeNaming.maxLength)))
        XCTAssertFalse(NodeNaming.isValid(""))
        XCTAssertFalse(NodeNaming.isValid("a b"))
        XCTAssertFalse(NodeNaming.isValid("a-b"))
        XCTAssertFalse(NodeNaming.isValid("é"))
        XCTAssertFalse(NodeNaming.isValid("/a"))
        XCTAssertFalse(NodeNaming.isValid("a.b"))
        XCTAssertFalse(NodeNaming.isValid(String(repeating: "a", count: NodeNaming.maxLength + 1)))
    }

    func testATypedNameIsRefusedWithAReason() {
        XCTAssertNil(NodeNaming.problem(with: "sol"))
        XCTAssertNotNil(NodeNaming.problem(with: ""))
        XCTAssertNotNil(NodeNaming.problem(with: "mon nœud"))
        let huge = String(repeating: "a", count: 100_000)
        let message = NodeNaming.problem(with: huge)
        XCTAssertNotNil(message)
        XCTAssertLessThan(message?.count ?? 0, 100, "the message does not repeat what was typed")
    }
}

final class NodeClipboardTests: XCTestCase {
    func testOnlyACopyOfNodesLooksLikeOne() {
        XCTAssertTrue(NodeClipboard.looksLikeCopy("{\n\"helixNodes\": {\n\"format\": \"helix.nodes\",\n\"version\": 1"))
        XCTAssertFalse(NodeClipboard.looksLikeCopy("hello"))
        XCTAssertFalse(NodeClipboard.looksLikeCopy(""))
        XCTAssertFalse(NodeClipboard.looksLikeCopy("{\"other\": 1}"))
    }

    func testACopyWithNoPlaceMovesOneStepFromTheOriginals() {
        let delta = NodeClipboard.offset(for: [CGPoint(x: 0, y: 0), CGPoint(x: 100, y: 0)], occupied: [CGPoint(x: 0, y: 0), CGPoint(x: 100, y: 0)], target: nil)
        XCTAssertEqual(delta, NodeClipboard.step)
    }

    func testPastingAgainMovesOnAStepSoTheNodesDoNotPileUp() {
        let original = CGPoint(x: 0, y: 0)
        let first = NodeClipboard.step
        let delta = NodeClipboard.offset(for: [original], occupied: [original, first], target: nil)
        XCTAssertEqual(delta, CGPoint(x: first.x * 2, y: first.y * 2))
    }

    func testACopyWithAPlaceCentresOnIt() {
        let delta = NodeClipboard.offset(for: [CGPoint(x: 0, y: 0), CGPoint(x: 200, y: 100)], occupied: [], target: CGPoint(x: 500, y: 300))
        XCTAssertEqual(delta, CGPoint(x: 400, y: 250))
        // A card that is on the spot already is passed, not covered.
        let passed = NodeClipboard.offset(for: [CGPoint(x: 0, y: 0)], occupied: [CGPoint(x: 500, y: 300)], target: CGPoint(x: 500, y: 300))
        XCTAssertEqual(passed, CGPoint(x: 540, y: 340))
    }

    func testNothingToPlaceNeedsNoMove() {
        XCTAssertEqual(NodeClipboard.offset(for: [], occupied: [], target: nil), .zero)
    }
}

final class AliasLabelTests: XCTestCase {
    let noise = EditorNodeType(name: "helix::attributeNoise", label: "Bruit d’attribut")
    let mountain = EditorNodeType(name: "helix::mountain", label: "Montagne")

    func graph(_ aliasType: String) -> (EditorGraph, EditorNode) {
        let node = EditorNode(id: 1, type: noise.name, aliasType: aliasType, name: "montagne1", path: "/montagne1", position: .zero)
        return (EditorGraph(nodes: [node], links: [], displayNode: nil, types: [noise.name: noise, mountain.name: mountain]), node)
    }

    func testANodeMadeFromAnAliasShowsTheAliasAndWhatItIsBasedOn() {
        let (graph, node) = graph("helix::mountain")
        XCTAssertEqual(graph.label(of: node), "Montagne")
        XCTAssertEqual(graph.basedOn(node), "Bruit d’attribut")
    }

    func testANodeMadeFromItsTypeShowsTheTypeAlone() {
        let (graph, node) = graph("")
        XCTAssertEqual(graph.label(of: node), "Bruit d’attribut")
        XCTAssertNil(graph.basedOn(node))
    }

    func testAnAliasThisBuildDoesNotKnowFallsBackToTheType() {
        let (graph, node) = graph("helix::gone")
        XCTAssertEqual(graph.label(of: node), "Bruit d’attribut")
        XCTAssertNil(graph.basedOn(node))
    }
}

@MainActor
final class NodeEditorEditingTests: XCTestCase {
    private var recorder: NodeEditorModelTests.Recorder!

    private func makeModel() -> NodeEditorModel {
        let cube = EditorNodeType(name: "helix::cube", label: "Cube")
        let model = NodeEditorModel()
        recorder = NodeEditorModelTests.Recorder()
        model.editing = recorder
        let nodes = [
            EditorNode(id: 1, type: cube.name, name: "a", path: "/a", position: CGPoint(x: 0, y: 0)),
            EditorNode(id: 2, type: cube.name, name: "b", path: "/b", position: CGPoint(x: 200, y: 0)),
        ]
        model.reload(EditorGraph(nodes: nodes, links: [], displayNode: nil, types: [cube.name: cube]))
        model.setViewSize(CGSize(width: 800, height: 600))
        return model
    }

    func testRenameNeedsExactlyOneSelectedNode() {
        let model = makeModel()
        model.beginRename()
        XCTAssertNil(model.renaming)
        model.select(nodes: [1, 2])
        model.beginRename()
        XCTAssertNil(model.renaming)
        model.select(nodes: [2])
        model.beginRename()
        XCTAssertEqual(model.renaming, RenameSession(node: 2, text: "b", error: nil))
    }

    func testReturnTakesTheNameAndEscapeLeavesTheOldOne() {
        let model = makeModel()
        model.beginRename(1)
        model.renameTextChanged("sol")
        model.commitRename()
        XCTAssertEqual(recorder.calls, ["rename 1 sol"])
        XCTAssertNil(model.renaming)

        model.beginRename(1)
        model.renameTextChanged("autre")
        model.cancelRename()
        XCTAssertNil(model.renaming)
        XCTAssertEqual(recorder.calls, ["rename 1 sol"])
    }

    func testANameThatIsTheNodesOwnChangesNothing() {
        let model = makeModel()
        model.beginRename(1)
        model.commitRename()
        XCTAssertEqual(recorder.calls, [])
        XCTAssertNil(model.renaming)
    }

    func testANameThatCannotBeTakenStaysInTheFieldWithItsReason() {
        let model = makeModel()
        model.beginRename(1)
        model.renameTextChanged("mon nœud")
        model.commitRename()
        XCTAssertEqual(recorder.calls, [], "a name the engine would refuse is not asked of it")
        XCTAssertNotNil(model.renaming?.error)

        // A name another node has is the engine's to refuse, and its words are shown.
        model.renameTextChanged("b")
        XCTAssertNil(model.renaming?.error, "typing clears the last refusal")
        recorder.renameError = "A node named 'b' already exists."
        model.commitRename()
        XCTAssertEqual(recorder.calls, ["rename 1 b"])
        XCTAssertEqual(model.renaming?.error, "A node named 'b' already exists.")
        XCTAssertEqual(model.renaming?.text, "b")

        recorder.renameError = nil
        model.renameTextChanged("c")
        model.commitRename()
        XCTAssertNil(model.renaming)
    }

    func testTheSpacesAroundATypedNameAreNotPartOfIt() {
        let model = makeModel()
        model.beginRename(1)
        model.renameTextChanged("  sol ")
        model.commitRename()
        XCTAssertEqual(recorder.calls, ["rename 1 sol"])
    }

    func testCopyCutAndDuplicateActOnTheSelectedNodesInGraphOrder() {
        let model = makeModel()
        model.copySelection()
        model.cutSelection()
        model.duplicateSelection()
        XCTAssertEqual(recorder.calls, [], "nothing is selected")
        model.select(nodes: [2, 1])
        model.copySelection()
        model.cutSelection()
        XCTAssertEqual(recorder.calls, ["copy [1, 2]", "cut [1, 2]"])
    }

    func testWhatWasPastedOrDuplicatedBecomesTheSelection() {
        let model = makeModel()
        let cube = EditorNodeType(name: "helix::cube", label: "Cube")
        model.select(nodes: [1])
        recorder.afterMaking = {
            let made = EditorNode(id: 51, type: cube.name, name: "a2", path: "/a2", position: CGPoint(x: 40, y: 40))
            model.reload(EditorGraph(nodes: model.graph.nodes + [made], links: [], displayNode: nil, types: [cube.name: cube]))
        }
        model.duplicateSelection()
        XCTAssertEqual(recorder.calls.last, "duplicate [1]")
        XCTAssertEqual(model.selectedNodes, [51])

        // A paste selects the nodes it made, and only those that are there.
        recorder.afterMaking = {
            let made = EditorNode(id: 41, type: cube.name, name: "a3", path: "/a3", position: CGPoint(x: 80, y: 80))
            model.reload(EditorGraph(nodes: model.graph.nodes + [made], links: [], displayNode: nil, types: [cube.name: cube]))
        }
        model.pasteFromPasteboard()
        XCTAssertEqual(model.selectedNodes, [41])
    }

    func testAPasteGoesUnderThePointerOnlyWhileItIsOverTheEditor() {
        let model = makeModel()
        model.pointer = CGPoint(x: 100, y: 100)
        model.pasteFromPasteboard()
        XCTAssertEqual(recorder.calls.last, "paste none")
        model.pointerInside = true
        model.pasteFromPasteboard()
        let canvas = model.transform.canvasPoint(CGPoint(x: 100, y: 100))
        XCTAssertEqual(recorder.calls.last, "paste \(canvas.x),\(canvas.y)")
    }

    func testAPasteOfNothingShowsTheRefusal() {
        let model = makeModel()
        recorder.canPaste = false
        XCTAssertFalse(model.canPaste)
    }
}

final class PathChangeTests: XCTestCase {
    func testTheSelectedPathFollowsTheRenameBothWays() {
        let change = PathChange(from: "/grille1", to: "/sol")
        XCTAssertEqual(change.apply(to: "/grille1"), "/sol")
        XCTAssertEqual(change.apply(to: "/autre"), "/autre")
        XCTAssertEqual(change.reversed.apply(to: "/sol"), "/grille1")
        XCTAssertEqual(change.reversed.reversed, change)
    }
}

// MARK: Lot 3, part B: a readable graph, port labels, tidy, the eye, the palette

final class ReadableFramingTests: XCTestCase {
    func testFramingNeverGoesBelowTheReadableZoom() {
        let wide = CGRect(x: 0, y: 0, width: 6000, height: 3000)
        let size = CGSize(width: 800, height: 400)
        let t = ViewTransform.framing(wide, in: size)
        XCTAssertEqual(t.zoom, 0.6, accuracy: 1e-9)
        XCTAssertGreaterThanOrEqual(ViewTransform.minZoom, 0.25)
    }

    func testAGraphThatDoesNotFitIsCentredOnTheFocus() {
        let wide = CGRect(x: 0, y: 0, width: 6000, height: 3000)
        let size = CGSize(width: 800, height: 400)
        let focus = CGPoint(x: 5000, y: 2500)
        let t = ViewTransform.framing(wide, in: size, focus: focus)
        let view = t.viewPoint(focus)
        XCTAssertEqual(view.x, 400, accuracy: 1e-6)
        XCTAssertEqual(view.y, 200, accuracy: 1e-6)
    }

    func testAGraphThatFitsIgnoresTheFocus() {
        let rect = CGRect(x: 0, y: 0, width: 400, height: 200)
        let size = CGSize(width: 800, height: 600)
        let t = ViewTransform.framing(rect, in: size, focus: CGPoint(x: 0, y: 0))
        XCTAssertEqual(t.viewRect(rect).midX, 400, accuracy: 1e-6)
        XCTAssertGreaterThan(t.zoom, 0.6)
    }

    @MainActor
    func testTheModelFramesAroundTheDisplayNode() {
        let cube = EditorNodeType(name: "helix::cube", label: "Cube")
        let nodes = (0..<40).map { EditorNode(id: UInt64($0 + 1), type: cube.name, name: "n\($0)", path: "/n\($0)", position: CGPoint(x: CGFloat($0) * 200, y: 0)) }
        let model = NodeEditorModel()
        model.reload(EditorGraph(nodes: nodes, links: [], displayNode: 40, types: [cube.name: cube]), frame: true)
        model.setViewSize(CGSize(width: 800, height: 400))
        XCTAssertEqual(model.transform.zoom, 0.6, accuracy: 1e-9)
        let view = model.transform.viewPoint(CGPoint(x: 39 * 200, y: 0))
        XCTAssertEqual(view.x, 400, accuracy: 1e-6)
    }
}

@MainActor
final class PortHoverAndEyeTests: XCTestCase {
    let cube = EditorNodeType(name: "helix::cube", label: "Cube")
    let boolean = EditorNodeType(name: "helix::boolean", label: "Boolean", inputs: [EditorPortInfo(label: "A"), EditorPortInfo(label: "B")])
    let importer = EditorNodeType(name: "helix::geometryImport", label: "Import", outputCount: 2)

    func graph(display: UInt64? = nil) -> EditorGraph {
        let nodes = [
            EditorNode(id: 1, type: boolean.name, name: "b", path: "/b", position: CGPoint(x: 0, y: 0)),
            EditorNode(id: 2, type: importer.name, name: "i", path: "/i", position: CGPoint(x: 400, y: 0)),
            EditorNode(id: 3, type: cube.name, name: "c", path: "/c", position: CGPoint(x: 0, y: 300)),
        ]
        return EditorGraph(nodes: nodes, links: [], displayNode: display,
                           types: [boolean.name: boolean, importer.name: importer, cube.name: cube])
    }

    func testAnUnlinkedInputIsFoundAndNamed() {
        let g = graph()
        let second = EditorLayout.inputPoint(of: g.node(1)!, type: boolean, index: 1, multiCount: 0)
        let port = EditorLayout.port(at: CGPoint(x: second.x + 2, y: second.y - 2), zoom: 1, in: g)
        XCTAssertEqual(port, .input(node: 1, index: 1))
        XCTAssertEqual(EditorLayout.label(of: port!, in: g), "B")
        XCTAssertNil(EditorLayout.port(at: CGPoint(x: 0, y: 0), zoom: 1, in: g))
    }

    func testOutputsAreNumberedOnlyWhenThereAreSeveral() {
        let g = graph()
        XCTAssertEqual(EditorLayout.label(of: .output(node: 3, index: 0), in: g), "Output")
        XCTAssertEqual(EditorLayout.label(of: .output(node: 2, index: 1), in: g), "Output 2")
        let second = EditorLayout.outputPoint(of: g.node(2)!, type: importer, index: 1)
        XCTAssertEqual(EditorLayout.port(at: second, zoom: 1, in: g), .output(node: 2, index: 1))
    }

    func testTheModelTracksTheHoverAndClearsItOnExit() {
        let model = NodeEditorModel()
        model.reload(graph())
        model.setViewSize(CGSize(width: 800, height: 600))
        let g = model.graph
        let port = EditorLayout.inputPoint(of: g.node(1)!, type: boolean, index: 0, multiCount: 0)
        model.mouseMoved(to: model.transform.viewPoint(port))
        XCTAssertEqual(model.hover.port, .input(node: 1, index: 0))
        XCTAssertEqual(model.hover.node, 1)
        model.mouseExited()
        XCTAssertEqual(model.hover, EditorHover())
    }

    func testTheEyeIsTheLeftStripOfTheCardAndNotAPort() {
        let g = graph()
        let strip = EditorLayout.eyeRect(at: g.node(3)!.position)
        XCTAssertEqual(strip.minX, EditorLayout.cardRect(at: g.node(3)!.position).minX)
        XCTAssertTrue(EditorLayout.isOverEye(CGPoint(x: strip.midX, y: strip.midY), of: g.node(3)!, zoom: 1))
        XCTAssertFalse(EditorLayout.isOverEye(CGPoint(x: strip.maxX + 5, y: strip.midY), of: g.node(3)!, zoom: 1))
        // Too small to draw, so it takes no clicks.
        XCTAssertFalse(EditorLayout.isOverEye(CGPoint(x: strip.midX, y: strip.midY), of: g.node(3)!, zoom: 0.4))
        // No port of a card with two inputs sits in the strip.
        for index in 0..<2 {
            let p = EditorLayout.inputPoint(of: g.node(1)!, type: boolean, index: index, multiCount: 0)
            XCTAssertFalse(EditorLayout.eyeRect(at: g.node(1)!.position).insetBy(dx: -EditorLayout.grabRadius, dy: 0).contains(p))
        }
    }

    func testAClickOnTheEyeShowsTheNodeAndADragFromItStillMoves() {
        let model = NodeEditorModel()
        let rec = NodeEditorModelTests.Recorder()
        model.editing = rec
        model.reload(graph())
        model.setViewSize(CGSize(width: 800, height: 600))
        model.zoom(by: 1, around: .zero)
        let strip = EditorLayout.eyeRect(at: CGPoint(x: 0, y: 300))
        let press = model.transform.viewPoint(CGPoint(x: strip.midX, y: strip.midY))
        model.mouseDown(at: press, clickCount: 1, modifiers: [])
        model.mouseUp(at: press)
        XCTAssertEqual(rec.calls, ["display 3"])
        XCTAssertEqual(model.selectedNodes, [3])

        rec.calls = []
        model.mouseDown(at: press, clickCount: 1, modifiers: [])
        model.mouseDragged(to: CGPoint(x: press.x + 40, y: press.y))
        model.mouseUp(at: CGPoint(x: press.x + 40, y: press.y))
        XCTAssertEqual(rec.calls, ["move"], "a drag from the eye moves the node and shows nothing")

        rec.calls = []
        model.mouseDown(at: press, clickCount: 1, modifiers: .shift)
        model.mouseUp(at: press)
        XCTAssertFalse(rec.calls.contains("display 3"), "a shift click only extends the selection")
    }
}

final class GraphTidyTests: XCTestCase {
    let one = EditorNodeType(name: "t::one", label: "One", inputs: [EditorPortInfo(label: "In")])
    let two = EditorNodeType(name: "t::two", label: "Two", inputs: [EditorPortInfo(label: "A"), EditorPortInfo(label: "B")])
    let source = EditorNodeType(name: "t::source", label: "Source")

    func node(_ id: UInt64, _ type: EditorNodeType, _ x: CGFloat, _ y: CGFloat) -> EditorNode {
        EditorNode(id: id, type: type.name, name: "n\(id)", path: "/n\(id)", position: CGPoint(x: x, y: y))
    }

    func link(_ a: UInt64, _ b: UInt64, _ input: Int = 0) -> EditorLink {
        EditorLink(sourceNode: a, sourceOutput: 0, targetNode: b, targetInput: input)
    }

    func graph(_ nodes: [EditorNode], _ links: [EditorLink]) -> EditorGraph {
        EditorGraph(nodes: nodes, links: links, displayNode: nil, types: [one.name: one, two.name: two, source.name: source])
    }

    func positions(_ g: EditorGraph, among ids: Set<UInt64>? = nil) -> [UInt64: CGPoint] {
        var result = Dictionary(uniqueKeysWithValues: g.nodes.map { ($0.id, $0.position) })
        for move in GraphTidy.moves(for: g, among: ids) { result[move.id] = move.position }
        return result
    }

    func testEveryLinkRunsDownAndRowsFollowTheLongestPath() {
        // 1 -> 2 -> 4, and 1 -> 4 as well: 4 sits two rows under 1, not one.
        let g = graph(
            [node(1, source, 500, 900), node(2, one, -300, 20), node(3, source, 40, 40), node(4, two, 700, 5)],
            [link(1, 2), link(2, 4, 0), link(3, 4, 1), link(1, 4, 1)]
        )
        let p = positions(g)
        XCTAssertLessThan(p[1]!.y, p[2]!.y)
        XCTAssertLessThan(p[2]!.y, p[4]!.y)
        XCTAssertEqual(p[3]!.y, p[1]!.y, "both sources share the first row")
        XCTAssertEqual(p[4]!.y - p[1]!.y, 2 * EditorLayout.chainSpacing, accuracy: 1e-6)
    }

    func testNoTwoCardsOverlapAndTheResultIsAnchoredAtTheTopLeft() {
        var nodes: [EditorNode] = []
        for id in 1...9 {
            let jitter = CGFloat(id * 7 % 5) * 3
            nodes.append(node(UInt64(id), id <= 4 ? source : one, 100 + jitter, 50 + CGFloat(id) * 2))
        }
        let links = [link(1, 5), link(2, 5), link(3, 6), link(4, 7), link(5, 8), link(6, 8), link(7, 9)]
        let g = graph(nodes, links)
        let p = positions(g)
        let cards = g.nodes.map { EditorLayout.cardRect(at: p[$0.id]!) }
        for i in cards.indices { for j in cards.indices where i < j { XCTAssertFalse(cards[i].intersects(cards[j])) } }
        let after = cards.dropFirst().reduce(cards[0]) { $0.union($1) }
        let before = g.bounds()!
        XCTAssertEqual(after.minX, before.minX, accuracy: 1e-6)
        XCTAssertEqual(after.minY, before.minY, accuracy: 1e-6)
    }

    /// The graph of the audit scene "grand graphe" (15 nodes, 13 links, several that skip rows),
    /// scattered, with every card taking any number of inputs.
    func bigGraph() -> EditorGraph {
        let many = EditorNodeType(name: "t::many", label: "Many", inputs: [EditorPortInfo(label: "In", multiInput: true)])
        let pairs: [(UInt64, UInt64)] = [(2, 1), (3, 2), (4, 3), (5, 4), (6, 3), (7, 6), (8, 6), (9, 3), (10, 9), (11, 10), (12, 9), (13, 12), (14, 13)]
        var nodes: [EditorNode] = []
        for id in UInt64(0)...14 {
            nodes.append(node(id, many, CGFloat((id * 389) % 1100) - 400, CGFloat((id * 173) % 500)))
        }
        var links: [EditorLink] = []
        var used: [UInt64: Int] = [:]
        for (source, target) in pairs {
            links.append(link(source, target, used[target, default: 0]))
            used[target, default: 0] += 1
        }
        return EditorGraph(nodes: nodes, links: links, displayNode: nil, types: [many.name: many])
    }

    func testNoWireRunsThroughACardAfterTidy() {
        let g = bigGraph()
        let p = positions(g)
        var tidied = g
        tidied = EditorGraph(
            nodes: g.nodes.map { var n = $0; n.position = p[$0.id]!; return n },
            links: g.links, displayNode: nil, types: g.types
        )
        var longLinks = 0
        for link in tidied.links {
            guard let (start, end) = EditorLayout.endpoints(of: link, in: tidied) else { continue }
            if end.y - start.y > 2 * EditorLayout.chainSpacing - 10 { longLinks += 1 }
            let curve = EditorLayout.curvePoints(from: start, to: end, steps: 60)
            for other in tidied.nodes where other.id != link.sourceNode && other.id != link.targetNode {
                let card = EditorLayout.cardRect(at: other.position)
                for point in curve {
                    XCTAssertFalse(card.contains(point), "the link \(link.sourceNode) to \(link.targetNode) runs through card \(other.id)")
                }
            }
        }
        XCTAssertGreaterThan(longLinks, 0, "the graph has links that skip rows")
    }

    func testSeparatePiecesSitSideBySide() {
        let g = graph([node(1, source, 0, 0), node(2, one, 0, 100), node(3, source, 0, 0), node(4, one, 0, 100)], [link(1, 2), link(3, 4)])
        let p = positions(g)
        XCTAssertEqual(p[1]!.y, p[3]!.y)
        XCTAssertGreaterThanOrEqual(abs(p[1]!.x - p[3]!.x), EditorLayout.cardSize.width)
    }

    func testTheParentOfTheFirstInputLeansLeft() {
        // 1 feeds input B and 2 feeds input A of node 3, but 1 was drawn on the left.
        let g = graph([node(1, source, 0, 0), node(2, source, 300, 0), node(3, two, 150, 150)], [link(1, 3, 1), link(2, 3, 0)])
        let p = positions(g)
        XCTAssertLessThan(p[2]!.x, p[1]!.x)
    }

    func testOnlyTheSelectionMovesAndASingleNodeMovesNothing() {
        let g = graph([node(1, source, 0, 0), node(2, one, 500, 400), node(3, source, 900, 900)], [link(1, 2)])
        let moves = GraphTidy.moves(for: g, among: [1, 2])
        XCTAssertFalse(moves.contains { $0.id == 3 })
        XCTAssertTrue(GraphTidy.moves(for: g, among: [3]).isEmpty)
        XCTAssertTrue(GraphTidy.moves(for: graph([node(1, source, 5, 5)], []), among: nil).isEmpty)
    }

    func testATidyGraphStaysWhereItIs() {
        let g = graph([node(1, source, 0, 0), node(2, one, 0, 120)], [link(1, 2)])
        XCTAssertTrue(GraphTidy.moves(for: g).isEmpty)
    }

    func testACycleDoesNotHang() {
        let g = graph([node(1, one, 0, 0), node(2, one, 0, 100)], [link(1, 2), link(2, 1)])
        XCTAssertEqual(positions(g).count, 2)
    }

    @MainActor
    func testTidyIsOneStepAndFramesTheGraph() {
        let g = graph([node(1, source, 900, 900), node(2, one, 10, 10)], [link(1, 2)])
        let model = NodeEditorModel()
        let rec = NodeEditorModelTests.Recorder()
        model.editing = rec
        model.reload(g)
        model.setViewSize(CGSize(width: 800, height: 600))
        model.tidyGraph()
        XCTAssertEqual(rec.calls, ["move"])
        XCTAssertEqual(rec.moves.count, 2)
    }
}

final class PaletteVisibilityTests: XCTestCase {
    func testTestNodesAreHiddenInEitherLanguage() {
        let rat = EditorNodeType(name: "helix::modelRat", label: "Modèle : rat", tags: ["modèle", "essai"], searchTags: ["model", "test"])
        let cube = EditorNodeType(name: "helix::cube", label: "Cube", tags: ["primitive"])
        XCTAssertTrue(rat.isTest)
        XCTAssertFalse(cube.isTest)
    }

    func testTheNodeWithoutTheSentenceStillShowsItsName() {
        XCTAssertEqual(EditorNodeType(name: "x::y", label: "Y").description, "")
    }
}

final class AttributeNoteTests: XCTestCase {
    func node(_ type: String, name: String?) -> InspectorNode {
        var node = InspectorNode(id: 1, name: "n", path: "/n", type: type, typeLabel: "T", isDisplay: false, error: nil)
        if let name { node.parameters["name"] = InspectorParameter(name: "name", label: "Name", strings: [name]) }
        return node
    }

    func testANodeThatWritesAnotherAttributeThanPositionHasANote() {
        XCTAssertEqual(node("helix::attributeNoise", name: "noise").writtenAttribute, "noise")
        XCTAssertEqual(node("helix::normal", name: "Normal").writtenAttribute, "Normal")
        XCTAssertEqual(node("helix::attributeCreate", name: "temperature").writtenAttribute, "temperature")
    }

    func testPositionOrAnotherTypeHasNone() {
        XCTAssertNil(node("helix::attributeNoise", name: "Position").writtenAttribute)
        XCTAssertNil(node("helix::attributeCreate", name: "").writtenAttribute)
        XCTAssertNil(node("helix::cube", name: "noise").writtenAttribute)
        XCTAssertNil(node("helix::attributeNoise", name: nil).writtenAttribute)
    }
}
