import XCTest

// The tests of what the last lot of the usability audit added: the skeleton chain, the export, the
// graph for a keyboard and for VoiceOver, and the widths of the spreadsheet.

final class SkeletonClipChoiceTests: XCTestCase {
    private let clips = [SkeletonClip(name: "Bend", duration: 1), SkeletonClip(name: "Snap", duration: 2.6)]

    func testTheMenuHasOneEntryPerClipAndOneForTheFirst() {
        let options = SkeletonClipChoices.options(clips: clips, current: "Bend")
        XCTAssertEqual(options.map(\.value), ["", "Bend", "Snap"])
        XCTAssertTrue(options[1].label.hasPrefix("Bend ("), options[1].label)
        XCTAssertTrue(options[2].label.contains("2"), options[2].label)
    }

    func testAClipTheSkeletonDoesNotHaveStaysInTheListAndIsMarkedMissing() {
        let options = SkeletonClipChoices.options(clips: clips, current: "Walk")
        XCTAssertEqual(options.map(\.value), ["", "Bend", "Snap", "Walk"])
        XCTAssertTrue(options.last?.label.contains("Walk") == true)
        XCTAssertNotEqual(options.last?.label, "Walk")
    }

    func testAnEmptyClipIsTheFirstOneAndIsNotMissing() {
        XCTAssertEqual(SkeletonClipChoices.options(clips: clips, current: "").count, 3)
    }

    func testTheLengthIsWrittenWithItsUnit() {
        let text = SkeletonClip(name: "Roar", duration: 2.6).durationText
        XCTAssertTrue(text.contains("2") && text.contains("6"), text)
        XCTAssertTrue(text.hasSuffix("s") || text.contains("sec"), text)
    }
}

final class SkeletonChainPlacementTests: XCTestCase {
    private let importType = EditorNodeType(name: "helix::geometryImport", label: "Import", inputs: [], outputCount: 2)
    private func graph(_ nodes: [EditorNode]) -> EditorGraph {
        EditorGraph(nodes: nodes, links: [], displayNode: nil, types: [importType.name: importType])
    }
    private func node(_ id: UInt64, _ x: CGFloat, _ y: CGFloat) -> EditorNode {
        EditorNode(id: id, type: importType.name, name: "n\(id)", path: "/n\(id)", position: CGPoint(x: x, y: y))
    }

    func testTheSkinSitsTwoRowsDownAndThePoseOneRowDownBesideIt() {
        let places = NodePlacement.skeletonChain(below: CGPoint(x: 100, y: 50), in: graph([node(1, 100, 50)]))
        XCTAssertEqual(places.deform, CGPoint(x: 100, y: 50 + 2 * EditorLayout.chainSpacing))
        XCTAssertEqual(places.pose.y, 50 + EditorLayout.chainSpacing)
        XCTAssertGreaterThan(places.pose.x, 100 + EditorLayout.cardSize.width - 1, "the pose clears the straight link")
    }

    func testACardInTheWayMovesTheNodeToTheRight() {
        let blocker = node(2, 100, 50 + 2 * EditorLayout.chainSpacing)
        let places = NodePlacement.skeletonChain(below: CGPoint(x: 100, y: 50), in: graph([node(1, 100, 50), blocker]))
        XCTAssertGreaterThan(places.deform.x, 100)
        XCTAssertFalse(EditorLayout.cardRect(at: places.deform).intersects(EditorLayout.cardRect(at: blocker.position)))
    }

    func testThePoseNeverLandsOnTheSkin() {
        // A graph whose free spot for the pose is the one the skin was given.
        let places = NodePlacement.skeletonChain(below: .zero, in: graph([node(1, 0, 0)]))
        XCTAssertFalse(EditorLayout.cardRect(at: places.pose).intersects(EditorLayout.cardRect(at: places.deform)))
    }
}

@MainActor
final class AnimateSkeletonInspectorTests: XCTestCase {
    private final class Editing: InspectorEditing {
        var node: InspectorNode
        var animated: [(UInt64, String?)] = []
        var outcome = EditOutcome.done
        init(_ node: InspectorNode) { self.node = node }
        func loadInspector(node: UInt64) -> InspectorNode? { self.node }
        func attributeSuggestions(node: UInt64, parameter: String) -> [String] { [] }
        func apply(_ edit: InspectorEdit, to node: UInt64, named name: String) -> EditOutcome { .done }
        func applyLive(_ edit: InspectorEdit, to node: UInt64) -> EditOutcome { .done }
        func beginContinuousEdit(named name: String) {}
        func endContinuousEdit() {}
        func setDisplay(_ node: UInt64) -> EditOutcome { .done }
        func rename(_ node: UInt64, to name: String) -> EditOutcome { .done }
        func animateSkeleton(importNode: UInt64, clip: String?) -> EditOutcome {
            animated.append((importNode, clip))
            return outcome
        }
    }

    private func rigged(_ clips: [SkeletonClip]?) -> InspectorNode {
        var node = InspectorNode(id: 7, name: "knight", path: "/knight", typeLabel: "Import", isDisplay: false)
        node.type = "helix::geometryImport"
        node.skeleton = clips.map { InspectorSkeleton(clips: $0) }
        return node
    }

    func testTheButtonAsksForTheChainWithTheClipPicked() {
        let editing = Editing(rigged([SkeletonClip(name: "Bend", duration: 1)]))
        let model = InspectorModel()
        model.editing = editing
        model.select(nodes: [7])
        model.animateSkeleton(clip: "Bend")
        XCTAssertEqual(editing.animated.count, 1)
        XCTAssertEqual(editing.animated.first?.0, 7)
        XCTAssertEqual(editing.animated.first?.1, "Bend")
    }

    func testANodeWithNoSkeletonAsksForNothing() {
        let editing = Editing(rigged(nil))
        let model = InspectorModel()
        model.editing = editing
        model.select(nodes: [7])
        model.animateSkeleton(clip: nil)
        XCTAssertTrue(editing.animated.isEmpty)
    }

    func testARefusalIsShownInTheInspector() {
        let editing = Editing(rigged([SkeletonClip(name: "Bend", duration: 1)]))
        editing.outcome = .failed("No such clip.")
        let model = InspectorModel()
        model.editing = editing
        model.select(nodes: [7])
        model.animateSkeleton(clip: "Walk")
        XCTAssertEqual(model.message, "No such clip.")
    }
}

final class ExportOptionsTests: XCTestCase {
    func testTheViewportSizeIsTheViewportInWholePixels() {
        let size = ExportImageSize.viewport.pixels(viewport: CGSize(width: 1690.4, height: 645.6))
        XCTAssertEqual(size.width, 1690)
        XCTAssertEqual(size.height, 646)
    }

    func testAFixedSizeDoesNotDependOnTheWindow() {
        XCTAssertEqual(ExportImageSize.fullHD.pixels(viewport: CGSize(width: 10, height: 10)).width, 1920)
        XCTAssertEqual(ExportImageSize.ultraHD.pixels(viewport: .zero).height, 2160)
    }

    func testAnEmptyOrHugeViewportIsHeldToWhatTheGPUCanBeAsked() {
        let empty = ExportImageSize.viewport.pixels(viewport: .zero)
        XCTAssertEqual(empty.width, 1)
        XCTAssertEqual(empty.height, 1)
        let huge = ExportImageSize.viewport.pixels(viewport: CGSize(width: 1e9, height: .infinity))
        XCTAssertEqual(huge.width, ExportImageSize.maxSide)
        XCTAssertEqual(huge.height, 1)
        XCTAssertEqual(ExportImageSize.viewport.pixels(viewport: CGSize(width: CGFloat.nan, height: 5)).width, 1)
    }

    func testTheNameStartsFromTheTitleWithoutTheSceneExtension() {
        XCTAssertEqual(ExportNaming.suggestedName(title: "Dragon.kut", fallback: "Image", extension: "png"), "Dragon.png")
        XCTAssertEqual(ExportNaming.suggestedName(title: "  ", fallback: "Image", extension: "png"), "Image.png")
        XCTAssertEqual(ExportNaming.suggestedName(title: "a/b: c", fallback: "x", extension: "obj"), "a-b- c.obj")
    }
}

final class NodeNavigationTests: XCTestCase {
    private func node(_ id: UInt64, _ x: CGFloat, _ y: CGFloat) -> EditorNode {
        EditorNode(id: id, type: "helix::cube", name: "n\(id)", path: "/n\(id)", position: CGPoint(x: x, y: y))
    }

    // 1 on top, 2 and 3 under it, 4 far to the right of 3.
    private var nodes: [EditorNode] { [node(1, 200, 0), node(2, 100, 120), node(3, 300, 120), node(4, 700, 140)] }

    func testTheArrowGoesToTheNearestCardThatWay() {
        // Two cards are as near and as far off the line: the lower id.
        XCTAssertEqual(NodeNavigation.nearest(from: 1, toward: .down, in: nodes), 2)
        XCTAssertEqual(NodeNavigation.nearest(from: 2, toward: .right, in: nodes), 3)
        XCTAssertEqual(NodeNavigation.nearest(from: 3, toward: .right, in: nodes), 4)
        XCTAssertEqual(NodeNavigation.nearest(from: 3, toward: .left, in: nodes), 2)
        XCTAssertEqual(NodeNavigation.nearest(from: 2, toward: .up, in: nodes), 1)
    }

    func testAStraightCardBeatsANearerOneToTheSide() {
        let list = [node(1, 0, 0), node(2, 60, 80), node(3, 0, 300)]
        XCTAssertEqual(NodeNavigation.nearest(from: 1, toward: .down, in: list), 2, "close and a little aside still counts")
        // A card far to the side loses to one straight under, even when it is nearer along the way.
        let far = [node(1, 0, 0), node(2, 400, 60), node(3, 0, 300)]
        XCTAssertEqual(NodeNavigation.nearest(from: 1, toward: .down, in: far), 3)
    }

    func testNothingThatWayIsNil() {
        XCTAssertNil(NodeNavigation.nearest(from: 1, toward: .up, in: nodes))
        XCTAssertNil(NodeNavigation.nearest(from: 4, toward: .right, in: nodes))
        XCTAssertNil(NodeNavigation.nearest(from: 99, toward: .down, in: nodes))
    }

    func testTiesGoToTheLowerId() {
        let list = [node(1, 0, 0), node(5, 100, 100), node(3, -100, 100)]
        XCTAssertEqual(NodeNavigation.nearest(from: 1, toward: .down, in: list), 3)
    }

    func testTheKeyCodesAreTheArrows() {
        XCTAssertEqual(NodeNavigation.Direction(keyCode: 123), .left)
        XCTAssertEqual(NodeNavigation.Direction(keyCode: 124), .right)
        XCTAssertEqual(NodeNavigation.Direction(keyCode: 125), .down)
        XCTAssertEqual(NodeNavigation.Direction(keyCode: 126), .up)
        XCTAssertNil(NodeNavigation.Direction(keyCode: 36))
    }

    func testWithNothingSelectedTheFirstArrowLandsOnTheDisplayedNodeOrTheTopLeftOne() {
        XCTAssertEqual(NodeNavigation.target(from: [], toward: .down, in: nodes, display: 3), 3)
        XCTAssertEqual(NodeNavigation.target(from: [], toward: .down, in: nodes, display: nil), 1)
        XCTAssertEqual(NodeNavigation.target(from: [], toward: .down, in: nodes, display: 99), 1)
        XCTAssertNil(NodeNavigation.target(from: [], toward: .down, in: [], display: nil))
    }

    func testFromSeveralCardsTheMoveStartsFromTheOneThatReachesFurthest() {
        XCTAssertEqual(NodeNavigation.target(from: [2, 3], toward: .right, in: nodes, display: nil), 4)
        XCTAssertEqual(NodeNavigation.target(from: [2, 3], toward: .left, in: nodes, display: nil), nil)
    }
}

final class NodeAccessibilityTextTests: XCTestCase {
    func testTheLabelIsTheNameAndTheType() {
        XCTAssertEqual(NodeAccessibilityText.label(name: "cube1", typeLabel: "Cube"), "cube1, Cube")
    }

    func testTheValueSaysTheErrorBeforeTheOtherStates() {
        let value = NodeAccessibilityText.value(error: "No input.", warningCount: 2, isDisplay: true, isSelected: true)
        XCTAssertTrue(value.hasPrefix("Error: No input."), value)
        XCTAssertTrue(value.contains("Displayed") && value.contains("Selected"), value)
        XCTAssertFalse(value.contains("Warnings"), "an error says it all")
    }

    func testAQuietCardHasNoValueAndAWarnedOneCountsThem() {
        XCTAssertEqual(NodeAccessibilityText.value(error: nil, warningCount: 0, isDisplay: false, isSelected: false), "")
        XCTAssertTrue(NodeAccessibilityText.value(error: nil, warningCount: 3, isDisplay: false, isSelected: false).contains("3"))
    }
}

final class SpreadsheetColumnWidthsTests: XCTestCase {
    func testAColumnIsAsWideAsItsWidestCellOrItsHeader() {
        XCTAssertEqual(SpreadsheetColumnWidths.natural(header: 20, samples: [60, 90, 40]), 90 + SpreadsheetColumnWidths.cellPadding)
        XCTAssertEqual(SpreadsheetColumnWidths.natural(header: 100, samples: [10]), 100 + SpreadsheetColumnWidths.headerPadding)
    }

    func testAColumnIsHeldBetweenTheLeastAndTheMost() {
        XCTAssertEqual(SpreadsheetColumnWidths.natural(header: 0, samples: []), SpreadsheetColumnWidths.minimum)
        XCTAssertEqual(SpreadsheetColumnWidths.natural(header: 5000, samples: [9000]), SpreadsheetColumnWidths.maximum)
    }

    func testTheRoomThatIsLeftGoesToTheNumberColumnsEqually() {
        let widths = SpreadsheetColumnWidths.distribute(
            natural: [60, 100, 100], flexible: [false, true, true], available: 600, spacing: 6
        )
        XCTAssertEqual(widths[0], 60)
        XCTAssertEqual(widths[1], widths[2])
        XCTAssertGreaterThan(widths[1], 100)
        XCTAssertLessThanOrEqual(widths.reduce(0, +) + 18, 600)
    }

    func testColumnsThatAlreadyFillTheTableAreLeftAlone() {
        let natural: [CGFloat] = [200, 200, 200]
        XCTAssertEqual(
            SpreadsheetColumnWidths.distribute(natural: natural, flexible: [true, true, true], available: 500, spacing: 6),
            natural
        )
    }

    func testNothingGrowsWhenNoColumnCan() {
        XCTAssertEqual(
            SpreadsheetColumnWidths.distribute(natural: [60, 44], flexible: [false, false], available: 900, spacing: 6),
            [60, 44]
        )
    }
}
