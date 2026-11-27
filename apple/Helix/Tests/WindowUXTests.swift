import SwiftUI
import XCTest

final class CountFormatTests: XCTestCase {
    private let english = Locale(identifier: "en_US")
    private let french = Locale(identifier: "fr_FR")

    func testASmallCountIsWrittenInFull() {
        XCTAssertEqual(CountFormat.abbreviated(0, locale: english), "0")
        XCTAssertEqual(CountFormat.abbreviated(999, locale: english), "999")
        XCTAssertEqual(CountFormat.abbreviated(9_999, locale: english), "9999")
    }

    func testThousandsAndMillionsKeepThreeDigits() {
        XCTAssertEqual(CountFormat.abbreviated(12_345, locale: english), "12.3 k")
        XCTAssertEqual(CountFormat.abbreviated(123_456, locale: english), "123 k")
        XCTAssertEqual(CountFormat.abbreviated(1_028_781, locale: english), "1.03 M")
        XCTAssertEqual(CountFormat.abbreviated(1_946_618, locale: english), "1.95 M")
        XCTAssertEqual(CountFormat.abbreviated(12_500_000, locale: english), "12.5 M")
    }

    func testTheDecimalSeparatorIsTheLocalesOwn() {
        XCTAssertEqual(CountFormat.abbreviated(1_028_781, locale: french), "1,03 M")
    }

    func testACountThatRoundsUpToTheNextUnitReadsAsIt() {
        XCTAssertEqual(CountFormat.abbreviated(999_999, locale: english), "1.00 M")
    }
}

final class InterfaceSizeTests: XCTestCase {
    func testMediumIsTheStandardSizeAndTheDefault() {
        XCTAssertEqual(InterfaceSize.default, .medium)
        XCTAssertEqual(InterfaceSize.medium.scale, 1)
        XCTAssertEqual(InterfaceSize.medium.points(13), 13)
        XCTAssertEqual(InterfaceSize.medium.spreadsheetRowHeight, 22)
    }

    func testTheSizesGrowInOrder() {
        let sizes = InterfaceSize.allCases
        XCTAssertEqual(sizes, [.small, .medium, .large])
        for (smaller, larger) in zip(sizes, sizes.dropFirst()) {
            XCTAssertLessThan(smaller.scale, larger.scale)
            XCTAssertLessThan(smaller.spreadsheetRowHeight, larger.spreadsheetRowHeight)
            XCTAssertLessThan(smaller.rulerHeight, larger.rulerHeight)
            XCTAssertLessThan(smaller.points(UIText.body.points), larger.points(UIText.body.points))
        }
    }

    func testTheChoiceIsKeptAndAnUnknownValueIsMedium() throws {
        let defaults = try XCTUnwrap(UserDefaults(suiteName: "InterfaceSizeTests-\(UUID().uuidString)"))
        XCTAssertEqual(InterfaceSize.stored(in: defaults), .medium)
        InterfaceSize.large.store(in: defaults)
        XCTAssertEqual(InterfaceSize.stored(in: defaults), .large)
        defaults.set("enormous", forKey: InterfaceSize.defaultsKey)
        XCTAssertEqual(InterfaceSize.stored(in: defaults), .medium)
    }

    func testAWindowIsEightyFivePercentOfTheScreenButNeverSmallerThanTheMinimum() {
        let large = CGRect(x: 0, y: 25, width: 2560, height: 1415)
        let size = WindowPlacement.helixSize(in: large)
        XCTAssertEqual(size.width, (2560 * 0.85).rounded())
        XCTAssertEqual(size.height, (1415 * 0.85).rounded())
        let small = CGRect(x: 0, y: 0, width: 1000, height: 650)
        // A screen smaller than the minimum gets a window as large as the screen, no larger.
        XCTAssertEqual(WindowPlacement.helixSize(in: small), CGSize(width: 1000, height: 650))
        let laptop = CGRect(x: 0, y: 0, width: 1280, height: 775)
        XCTAssertEqual(WindowPlacement.helixSize(in: laptop), CGSize(width: 1100, height: 720))
    }
}

final class NodeCategoryTests: XCTestCase {
    private func type(_ name: String, tags: [String] = []) -> EditorNodeType {
        EditorNodeType(name: "helix::" + name, label: name.capitalized, tags: tags)
    }

    func testTypesFallIntoTheGroupTheirTagsSay() {
        XCTAssertEqual(NodeCategory.of(type("cube", tags: ["primitive", "surface"])), .primitives)
        XCTAssertEqual(NodeCategory.of(type("transform", tags: ["transform", "utility"])), .modify)
        XCTAssertEqual(NodeCategory.of(type("skeletonPose", tags: ["skeleton", "animation"])), .animation)
        XCTAssertEqual(NodeCategory.of(type("simulation", tags: ["simulation", "animation"])), .simulation)
        XCTAssertEqual(NodeCategory.of(type("positionNoise")), .modify)
        XCTAssertEqual(NodeCategory.of(type("mystery", tags: ["unheard"])), .others)
        // Translated tags are searched with the English ones.
        var french = type("cube", tags: ["primitive"])
        french.searchTags = ["primitive"]
        XCTAssertEqual(NodeCategory.of(french), .primitives)
    }

    func testTheImportNodeIsNotInAnyGroupAndTheGroupsKeepTheirOrder() {
        let types = [
            type("geometryImport", tags: ["import", "primitive"]), type("sphere", tags: ["primitive"]),
            type("cube", tags: ["primitive"]), type("transform", tags: ["transform"]), type("mystery"),
        ]
        let groups = NodeCategory.groups(of: types)
        XCTAssertEqual(groups.map(\.category), [.primitives, .modify, .others])
        XCTAssertEqual(groups[0].types.map(\.label), ["Cube", "Sphere"])
        XCTAssertNil(NodeCategory.of(types[0]))
    }

    func testAnAliasThatShowsTwiceInAGroupShowsOnce() {
        let groups = NodeCategory.groups(of: [type("noiseA", tags: ["noise"]), EditorNodeType(name: "helix::noiseB", label: "Noisea", tags: ["noise"])])
        XCTAssertEqual(groups.first?.types.count, 1)
    }
}

@MainActor
final class NetworkFramingTests: XCTestCase {
    private func graph(at position: CGPoint) -> EditorGraph {
        let cube = EditorNodeType(name: "helix::cube", label: "Cube")
        return EditorGraph(
            nodes: [EditorNode(id: 1, type: cube.name, name: "cube1", path: "/cube1", position: position)],
            links: [], displayNode: 1, types: [cube.name: cube])
    }

    func testAGraphLoadedBeforeTheViewHasASizeIsFramedOnceItHasOne() {
        let model = NodeEditorModel()
        model.reload(graph(at: .zero), frame: true)
        model.setViewSize(CGSize(width: 900, height: 400))
        let card = model.transform.viewRect(EditorLayout.cardRect(at: .zero))
        XCTAssertEqual(card.midX, 450, accuracy: 1)
        XCTAssertEqual(card.midY, 200, accuracy: 1)
        // Never smaller than a card can be read.
        XCTAssertGreaterThanOrEqual(model.transform.zoom, 0.8)
    }

    func testTheGraphStaysFramedAsTheWindowSettlesUntilThePersonMovesTheView() {
        let model = NodeEditorModel()
        model.reload(graph(at: CGPoint(x: 500, y: 300)), frame: true)
        model.setViewSize(CGSize(width: 300, height: 200))
        model.setViewSize(CGSize(width: 1200, height: 500))
        XCTAssertEqual(model.transform.viewRect(EditorLayout.cardRect(at: CGPoint(x: 500, y: 300))).midX, 600, accuracy: 1)
        model.pan(by: CGPoint(x: 40, y: 0))
        let moved = model.transform
        model.setViewSize(CGSize(width: 1400, height: 500))
        XCTAssertEqual(model.transform, moved)
    }

    func testALargerInterfaceFramesASmallGraphLarger() {
        let small = NodeEditorModel(), large = NodeEditorModel()
        large.setInterfaceScale(1.25)
        for model in [small, large] {
            model.reload(graph(at: .zero), frame: true)
            model.setViewSize(CGSize(width: 1200, height: 600))
        }
        XCTAssertGreaterThan(large.transform.zoom, small.transform.zoom)
    }
}
