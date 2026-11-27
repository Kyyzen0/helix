import XCTest

final class SpreadsheetColumnsTests: XCTestCase {
    private func report() -> SpreadsheetReport {
        SpreadsheetReport(
            generation: 5, pointCount: 4, vertexCount: 6, faceCount: 2, primitiveCount: 1,
            attributes: [
                SpreadsheetAttribute(owner: .points, name: "Position", type: .vector3, componentCount: 3, elementCount: 4),
                SpreadsheetAttribute(owner: .points, name: "noise", type: .float, componentCount: 1, elementCount: 4),
                SpreadsheetAttribute(owner: .vertices, name: "uv", type: .vector3, componentCount: 3, elementCount: 6),
                SpreadsheetAttribute(owner: .faces, name: "material_index", type: .int, componentCount: 1, elementCount: 2),
                SpreadsheetAttribute(owner: .faces, name: "visible", type: .bool, componentCount: 1, elementCount: 2),
                SpreadsheetAttribute(owner: .primitives, name: "xform", type: .matrix4, componentCount: 16, elementCount: 1),
            ]
        )
    }

    func testAComponentIsAColumnAndAVectorHasThree() {
        let titles = SpreadsheetColumns.columns(for: .points, in: report()).map(\.title)
        XCTAssertEqual(titles, ["#", "P.x", "P.y", "P.z", "noise"])
        XCTAssertEqual(SpreadsheetColumns.columns(for: .vertices, in: report()).map(\.title), ["#", "uv.x", "uv.y", "uv.z"])
    }

    func testAMatrixIsSixteenColumns() {
        let columns = SpreadsheetColumns.columns(for: .primitives, in: report())
        XCTAssertEqual(columns.count, 1 + 16)
        XCTAssertEqual(columns[1].title, "xform.0")
        XCTAssertEqual(columns.last?.title, "xform.15")
    }

    func testTheHeaderTooltipGivesTheNameAndTheType() {
        let column = SpreadsheetColumns.columns(for: .points, in: report())[1]
        XCTAssertTrue(column.tooltip.contains("Position"))
        XCTAssertTrue(column.tooltip.contains("vector3"))
    }

    func testThePrimitiveColumnAppearsOnlyWithSeveralPrimitives() {
        var several = report()
        several.primitiveCount = 2
        XCTAssertTrue(SpreadsheetColumns.columns(for: .points, in: several).contains { $0.kind == .primitive })
        XCTAssertFalse(SpreadsheetColumns.columns(for: .points, in: report()).contains { $0.kind == .primitive })
        XCTAssertFalse(SpreadsheetColumns.columns(for: .primitives, in: several).contains { $0.kind == .primitive })
    }

    func testAnAttributeOnSomePrimitivesOnlyIsMarkedPartial() {
        var partial = report()
        partial.attributes[1].elementCount = 2
        let column = SpreadsheetColumns.columns(for: .points, in: partial).last!
        XCTAssertTrue(column.partial)
        XCTAssertFalse(SpreadsheetColumns.columns(for: .points, in: report()).last!.partial)
    }

    func testTheRowsOfEachPrimitiveAreFoundByTheirStart() {
        var report = SpreadsheetReport(pointCount: 10, primitiveCount: 4)
        report.primitives = [
            SpreadsheetPrimitiveRange(pointStart: 0, pointCount: 3),
            SpreadsheetPrimitiveRange(pointStart: 3, pointCount: 0),
            SpreadsheetPrimitiveRange(pointStart: 3, pointCount: 5),
            SpreadsheetPrimitiveRange(pointStart: 8, pointCount: 2),
        ]
        let found = (0..<10).map { SpreadsheetColumns.primitive(ofRow: $0, owner: .points, in: report) }
        XCTAssertEqual(found, [0, 0, 0, 2, 2, 2, 2, 2, 3, 3])
        XCTAssertNil(SpreadsheetColumns.primitive(ofRow: 10, owner: .points, in: report))
        XCTAssertEqual(SpreadsheetColumns.primitive(ofRow: 2, owner: .primitives, in: report), 2)
    }

    func testTheOwnersAreReadFromTheCommandLine() {
        XCTAssertEqual(SpreadsheetOwner(name: "Points"), .points)
        XCTAssertEqual(SpreadsheetOwner(name: "vertex"), .vertices)
        XCTAssertEqual(SpreadsheetOwner(name: "faces"), .faces)
        XCTAssertEqual(SpreadsheetOwner(name: "primitive"), .primitives)
        XCTAssertNil(SpreadsheetOwner(name: "edges"))
    }
}

final class SpreadsheetFormatTests: XCTestCase {
    func testFloatsAreCompact() {
        XCTAssertEqual(SpreadsheetFormat.float(0), "0")
        XCTAssertEqual(SpreadsheetFormat.float(-0.0), "0")
        XCTAssertEqual(SpreadsheetFormat.float(1), "1")
        XCTAssertEqual(SpreadsheetFormat.float(0.5), "0.5")
        XCTAssertEqual(SpreadsheetFormat.float(1.0 / 3), "0.333333")
        XCTAssertEqual(SpreadsheetFormat.float(-2.25), "-2.25")
        XCTAssertEqual(SpreadsheetFormat.float(123456), "123456")
        XCTAssertEqual(SpreadsheetFormat.float(1.5e-7), "1.5e-07")
        XCTAssertEqual(SpreadsheetFormat.float(2.5e10), "2.5e+10")
    }

    func testTheOddOnesAreNamed() {
        XCTAssertEqual(SpreadsheetFormat.float(.nan), "nan")
        XCTAssertEqual(SpreadsheetFormat.float(.infinity), "inf")
        XCTAssertEqual(SpreadsheetFormat.float(-.infinity), "-inf")
    }

    func testIntegersAreIntegersAndBooleansAreWords() {
        XCTAssertEqual(SpreadsheetFormat.int(-7), "-7")
        XCTAssertEqual(SpreadsheetFormat.int(9_007_199_254_740_993), "9007199254740993", "no float in between")
        XCTAssertEqual(SpreadsheetFormat.bool(1), "true")
        XCTAssertEqual(SpreadsheetFormat.bool(0), "false")
    }

    func testAPageGivesTheTextOfAComponent() {
        let page = SpreadsheetPage(
            generation: 1, totalCount: 2, componentCount: 3, type: .vector3, floats: [1, 2, 3, 4, 5.5, 6]
        )
        XCTAssertEqual(page.rowCount, 2)
        XCTAssertEqual(page.text(row: 1, component: 1), "5.5")
        XCTAssertNil(page.text(row: 2, component: 0))
        XCTAssertNil(page.text(row: 0, component: 3))
        let flags = SpreadsheetPage(generation: 1, totalCount: 2, componentCount: 1, type: .bool, ints: [1, 0])
        XCTAssertEqual(flags.text(row: 1, component: 0), "false")
    }
}

final class PageCacheTests: XCTestCase {
    private func page(generation: UInt64, rows: Int = 4) -> SpreadsheetPage {
        SpreadsheetPage(generation: generation, totalCount: 100, componentCount: 1, type: .float,
                        floats: Array(repeating: 1, count: rows))
    }

    func testAPageStoredIsFoundAgain() {
        var cache = PageCache(pageSize: 4)
        cache.reset(generation: 3)
        XCTAssertNil(cache.page(attribute: "a", page: 0))
        XCTAssertTrue(cache.store(page(generation: 3), attribute: "a", number: 0))
        XCTAssertNotNil(cache.page(attribute: "a", page: 0))
        XCTAssertNil(cache.page(attribute: "b", page: 0))
        XCTAssertNil(cache.page(attribute: "a", page: 1))
    }

    func testAPageFromAnotherCookEmptiesTheCache() {
        var cache = PageCache(pageSize: 4)
        cache.reset(generation: 3)
        cache.store(page(generation: 3), attribute: "a", number: 0)
        cache.store(page(generation: 3), attribute: "a", number: 1)
        XCTAssertEqual(cache.count, 2)
        // The node cooked again while a page was being read: everything before is out of date.
        XCTAssertFalse(cache.store(page(generation: 4), attribute: "a", number: 5))
        XCTAssertEqual(cache.count, 1)
        XCTAssertEqual(cache.generation, 4)
        XCTAssertNil(cache.page(attribute: "a", page: 0))
    }

    func testResetForgetsEverything() {
        var cache = PageCache(pageSize: 4)
        cache.store(page(generation: 0), attribute: "a", number: 0)
        cache.reset(generation: 9)
        XCTAssertEqual(cache.count, 0)
        XCTAssertEqual(cache.generation, 9)
    }

    func testTheOldestPageGoesWhenTheCacheIsFull() {
        var cache = PageCache(pageSize: 4, capacity: 3)
        for number in 0..<5 { cache.store(page(generation: 0), attribute: "a", number: number) }
        XCTAssertEqual(cache.count, 3)
        XCTAssertNil(cache.page(attribute: "a", page: 0))
        XCTAssertNotNil(cache.page(attribute: "a", page: 4))
    }

    func testTheRowIsLocatedInItsPage() {
        let cache = PageCache(pageSize: 512)
        XCTAssertEqual(cache.locate(row: 0).page, 0)
        XCTAssertEqual(cache.locate(row: 511).page, 0)
        XCTAssertEqual(cache.locate(row: 512).page, 1)
        XCTAssertEqual(cache.locate(row: 1_000_000).row, 1_000_000 % 512)
    }
}

@MainActor
final class FakeSpreadsheetSource: SpreadsheetSource {
    var generation: UInt64 = 1
    var failure: String?
    var points = 3000
    var reads: [(attribute: String, start: Int, count: Int)] = []
    /// A read that cooks the node again, as a node that was dirty does.
    var bumpOnRead = false

    func nodeName(_ node: UInt64) -> String? { "grid\(node)" }

    func report(node: UInt64) -> Result<SpreadsheetReport, SpreadsheetFailure> {
        if let failure { return .failure(SpreadsheetFailure(message: failure)) }
        return .success(SpreadsheetReport(
            generation: generation, pointCount: points, primitiveCount: 1,
            attributes: [
                SpreadsheetAttribute(owner: .points, name: "Position", type: .vector3, componentCount: 3, elementCount: points),
                SpreadsheetAttribute(owner: .points, name: "id", type: .int, componentCount: 1, elementCount: points),
            ]
        ))
    }

    func readPage(node: UInt64, owner: SpreadsheetOwner, attribute: String, start: Int, count: Int) -> SpreadsheetPage? {
        reads.append((attribute, start, count))
        if bumpOnRead { generation += 1 }
        let rows = max(0, min(count, points - start))
        if attribute == "id" {
            return SpreadsheetPage(generation: generation, totalCount: points, componentCount: 1, type: .int,
                                   ints: (0..<rows).map { Int64(start + $0) })
        }
        var floats: [Float] = []
        for row in 0..<rows { floats += [Float(start + row), Float(generation), 0] }
        return SpreadsheetPage(generation: generation, totalCount: points, componentCount: 3, type: .vector3, floats: floats)
    }
}

@MainActor
final class SpreadsheetModelTests: XCTestCase {
    private var source = FakeSpreadsheetSource()
    private var model = SpreadsheetModel()

    override func setUp() async throws {
        source = FakeSpreadsheetSource()
        model = SpreadsheetModel()
        model.source = source
    }

    private func column(_ title: String) -> SpreadsheetColumn { model.columns.first { $0.title == title }! }

    func testWithNoNodeThereIsNothingToShow() {
        model.follow(node: nil)
        guard case .empty = model.state else { return XCTFail("state \(model.state)") }
        XCTAssertTrue(model.columns.isEmpty)
    }

    func testFollowingANodeBuildsTheColumnsAndCountsTheRows() {
        model.follow(node: 3)
        XCTAssertEqual(model.state, .table)
        XCTAssertEqual(model.rowCount, 3000)
        XCTAssertEqual(model.columns.map(\.title), ["#", "P.x", "P.y", "P.z", "id"])
        XCTAssertEqual(model.nodeName, "grid3")
    }

    func testOnlyThePagesOfTheCellsAskedForAreRead() {
        model.follow(node: 3)
        XCTAssertTrue(source.reads.isEmpty, "following a node reads no values")
        XCTAssertEqual(model.cellText(row: 0, column: column("P.x")), "0")
        XCTAssertEqual(model.cellText(row: 1, column: column("P.x")), "1")
        XCTAssertEqual(model.cellText(row: 511, column: column("P.x")), "511")
        XCTAssertEqual(source.reads.count, 1, "one page serves 512 rows")
        XCTAssertEqual(source.reads[0].count, 512)
        XCTAssertEqual(model.cellText(row: 2900, column: column("id")), "2900")
        XCTAssertEqual(source.reads.count, 2, "a row of a far page reads that page alone")
        XCTAssertEqual(source.reads[1].start, 2560)
    }

    func testAPageReadIsKeptWhileScrollingBack() {
        model.follow(node: 3)
        _ = model.cellText(row: 0, column: column("id"))
        _ = model.cellText(row: 700, column: column("id"))
        _ = model.cellText(row: 5, column: column("id"))
        XCTAssertEqual(source.reads.count, 2)
    }

    func testANewCookDropsThePages() {
        model.follow(node: 3)
        _ = model.cellText(row: 0, column: column("id"))
        XCTAssertEqual(source.reads.count, 1)
        source.generation = 2
        model.refresh()
        _ = model.cellText(row: 0, column: column("id"))
        XCTAssertEqual(source.reads.count, 2, "the page was read again")
    }

    func testTheSameCookKeepsThePages() {
        model.follow(node: 3)
        _ = model.cellText(row: 0, column: column("id"))
        let revision = model.revision
        model.refresh()
        _ = model.cellText(row: 0, column: column("id"))
        XCTAssertEqual(source.reads.count, 1)
        XCTAssertEqual(model.revision, revision, "nothing changed, so the table is not redrawn")
    }

    func testAnotherNodeDoesNotReuseThePagesOfThisOne() {
        model.follow(node: 3)
        _ = model.cellText(row: 0, column: column("id"))
        model.follow(node: 4)
        _ = model.cellText(row: 0, column: column("id"))
        XCTAssertEqual(source.reads.count, 2)
    }

    func testAReadThatCooksTheNodeAgainRedrawsTheTable() {
        model.follow(node: 3)
        let revision = model.revision
        source.bumpOnRead = true
        _ = model.cellText(row: 0, column: column("id"))
        let expectation = expectation(description: "refreshed")
        DispatchQueue.main.async { DispatchQueue.main.async { expectation.fulfill() } }
        wait(for: [expectation], timeout: 2)
        XCTAssertGreaterThan(model.revision, revision)
    }

    func testANodeInErrorShowsTheErrorAndNoTable() {
        source.failure = "/noise: The attribute needs a name."
        model.follow(node: 3)
        XCTAssertEqual(model.state, .failed("/noise: The attribute needs a name."))
        XCTAssertTrue(model.columns.isEmpty)
        XCTAssertEqual(model.rowCount, 0)
        // It comes back when the node is fixed.
        source.failure = nil
        model.refresh()
        XCTAssertEqual(model.state, .table)
        XCTAssertEqual(model.rowCount, 3000)
    }

    func testTheOwnerChangesTheRowsAndTheColumns() {
        model.follow(node: 3)
        model.owner = .vertices
        XCTAssertEqual(model.rowCount, 0)
        XCTAssertEqual(model.columns.map(\.title), ["#"])
        model.owner = .points
        XCTAssertEqual(model.rowCount, 3000)
    }

    func testAnAttributeThatIsNotOnEveryPrimitiveIsNotShownAgainstTheRows() {
        source.points = 3000
        model.follow(node: 3)
        let partial = SpreadsheetColumn(kind: .component(attribute: "id", component: 0), id: "x", title: "x", tooltip: "",
                                        type: .int, partial: true)
        XCTAssertEqual(model.cellText(row: 0, column: partial), "–")
        XCTAssertTrue(source.reads.isEmpty)
    }

    func testARowPastTheEndHasNoText() {
        model.follow(node: 3)
        XCTAssertEqual(model.cellText(row: 3000, column: column("id")), "")
        XCTAssertEqual(model.cellText(row: -1, column: column("id")), "")
    }

    func testTheRowNumberIsTheIndex() {
        model.follow(node: 3)
        XCTAssertEqual(model.cellText(row: 41, column: column("#")), "41")
        XCTAssertTrue(source.reads.isEmpty)
    }

    func testAHiddenTableDoesNotReadTheNode() {
        model.isShown = false
        model.follow(node: 3)
        XCTAssertEqual(model.rowCount, 0)
        model.isShown = true
        XCTAssertEqual(model.rowCount, 3000, "it reads when it comes into view")
    }

    func testCopyTakesTheHeaderAndTheRowsAsTabSeparatedText() {
        model.follow(node: 3)
        let text = model.copyText(rows: [2, 5])
        XCTAssertEqual(text, "#\tP.x\tP.y\tP.z\tid\n2\t2\t1\t0\t2\n5\t5\t1\t0\t5\n")
    }

    func testCopyRefusesAnEnormousSelection() {
        model.follow(node: 3)
        XCTAssertNil(model.copyText(rows: Array(0...SpreadsheetCopy.maxRows)))
    }
}
