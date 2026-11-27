import XCTest

final class NumberTextTests: XCTestCase {
    func testFormatTrimsAndRounds() {
        XCTAssertEqual(NumberText.format(1.0), "1")
        XCTAssertEqual(NumberText.format(0.5), "0.5")
        XCTAssertEqual(NumberText.format(-2.25), "-2.25")
        XCTAssertEqual(NumberText.format(0.123456), "0.1235")
        XCTAssertEqual(NumberText.format(100), "100")
        XCTAssertEqual(NumberText.format(0.0), "0")
        // A value that rounds to nothing is not written as "-0".
        XCTAssertEqual(NumberText.format(-0.00001), "-1e-05")
        XCTAssertEqual(NumberText.format(-0.0), "0")
        XCTAssertEqual(NumberText.format(3), "3")
    }

    func testFormatLargeAndNonFinite() {
        XCTAssertEqual(NumberText.format(1.5e12), "1.5e+12")
        XCTAssertEqual(NumberText.format(.infinity), "inf")
        XCTAssertEqual(NumberText.format(Double.nan), "nan")
    }

    func testParseFloat() {
        XCTAssertEqual(NumberText.parseFloat("2.5"), .value(2.5))
        XCTAssertEqual(NumberText.parseFloat("  -3 "), .value(-3))
        XCTAssertEqual(NumberText.parseFloat("1,5"), .value(1.5))
        XCTAssertEqual(NumberText.parseFloat("1e3"), .value(1000))
        XCTAssertEqual(NumberText.parseFloat("+4"), .value(4))
    }

    func testParseFloatRefusesWhatIsNotANumber() {
        for text in ["", "  ", "abc", "nan", "inf", "-inf", "1,2,3", "0x10", "1..2", "1e999", "12 13"] {
            guard case .invalid = NumberText.parseFloat(text) else {
                XCTFail("\(text) should be refused")
                continue
            }
        }
    }

    func testParseIntDoesNotRound() {
        XCTAssertEqual(NumberText.parseInt("12"), .value(12))
        XCTAssertEqual(NumberText.parseInt("-4"), .value(-4))
        XCTAssertEqual(NumberText.parseInt("3.0"), .value(3))
        guard case .invalid = NumberText.parseInt("2.5") else { return XCTFail("2.5 is not a whole number") }
        guard case .invalid = NumberText.parseInt("x") else { return XCTFail("x is not a number") }
        guard case .invalid = NumberText.parseInt("") else { return XCTFail("empty is not a number") }
        guard case .invalid = NumberText.parseInt("1e30") else { return XCTFail("too big for an Int") }
    }
}

final class RangeTests: XCTestCase {
    func testLockedEndsLimitAndUnlockedEndsDoNot() {
        let lowerLocked = InspectorRange(min: 0, max: 10, minLocked: true)
        XCTAssertEqual(lowerLocked.clamped(-5.0), 0)
        XCTAssertEqual(lowerLocked.clamped(50.0), 50, "an unlocked end is only where a slider stops")
        let bothLocked = InspectorRange(min: 1, max: 3, minLocked: true, maxLocked: true)
        XCTAssertEqual(bothLocked.clamped(7.0), 3)
        XCTAssertEqual(bothLocked.clamped(2.0), 2)
        let free = InspectorRange(min: 0, max: 1)
        XCTAssertEqual(free.clamped(-100.0), -100)
    }

    func testIntegerBoundsRoundInward() {
        let range = InspectorRange(min: 2.5, max: 9.5, minLocked: true, maxLocked: true)
        XCTAssertEqual(range.clamped(0), 3)
        XCTAssertEqual(range.clamped(20), 9)
        XCTAssertEqual(range.clamped(5), 5)
    }

    func testSliderMapping() {
        let range = InspectorRange(min: -10, max: 10)
        XCTAssertEqual(SliderMapping.fraction(of: 0, in: range), 0.5, accuracy: 1e-12)
        XCTAssertEqual(SliderMapping.fraction(of: -10, in: range), 0)
        // A value typed past an unlocked end pins the thumb at the end.
        XCTAssertEqual(SliderMapping.fraction(of: 500, in: range), 1)
        XCTAssertEqual(SliderMapping.fraction(of: -500, in: range), 0)
        XCTAssertEqual(SliderMapping.value(atFraction: 0.75, in: range), 5, accuracy: 1e-12)
        XCTAssertEqual(SliderMapping.value(atFraction: 2, in: range), 10)
        XCTAssertEqual(SliderMapping.value(atFraction: 0.34, in: InspectorRange(min: 0, max: 10), integer: true), 3)
    }

    func testDegenerateRangeHasNoSlider() {
        let range = InspectorRange(min: 5, max: 5)
        XCTAssertFalse(range.hasSlider)
        XCTAssertEqual(SliderMapping.fraction(of: 5, in: range), 0)
        XCTAssertFalse(InspectorRange(min: 0, max: .infinity).hasSlider)
        XCTAssertTrue(InspectorRange(min: 0, max: 1).hasSlider)
    }
}

final class ScrubTests: XCTestCase {
    func testRangeScrubMovesAsASliderWould() {
        let range = InspectorRange(min: 0, max: 10)
        XCTAssertEqual(Scrub.value(from: 5, translation: 150, range: range, integer: false), 10, accuracy: 1e-9)
        XCTAssertEqual(Scrub.value(from: 5, translation: -30, range: range, integer: false), 4, accuracy: 1e-9)
    }

    func testFineScrubIsSlower() {
        let range = InspectorRange(min: 0, max: 10)
        let coarse = Scrub.value(from: 5, translation: 30, range: range, integer: false)
        let fine = Scrub.value(from: 5, translation: 30, range: range, integer: false, fine: true)
        XCTAssertEqual(fine - 5, (coarse - 5) * Scrub.fineFactor, accuracy: 1e-9)
    }

    func testLockedEndsHoldAScrubAndUnlockedOnesDoNot() {
        let locked = InspectorRange(min: 0, max: 10, minLocked: true, maxLocked: true)
        XCTAssertEqual(Scrub.value(from: 1, translation: -900, range: locked, integer: false), 0)
        XCTAssertEqual(Scrub.value(from: 9, translation: 900, range: locked, integer: false), 10)
        let free = InspectorRange(min: 0, max: 10)
        XCTAssertGreaterThan(Scrub.value(from: 9, translation: 900, range: free, integer: false), 10)
    }

    func testIntegerScrubMovesInWholeSteps() {
        let range = InspectorRange(min: 3, max: 64, minLocked: true)
        XCTAssertEqual(Scrub.value(from: 24, translation: 13, range: range, integer: true), 26)
        XCTAssertEqual(Scrub.value(from: 24, translation: -3, range: range, integer: true), 24)
        XCTAssertEqual(Scrub.value(from: 24, translation: -600, range: range, integer: true), 3)
    }

    func testNoRangeScalesWithTheValueAndIgnoresGarbage() {
        XCTAssertGreaterThan(Scrub.value(from: 1000, translation: 10, range: nil, integer: false), 1000)
        XCTAssertEqual(Scrub.value(from: 7, translation: .nan, range: nil, integer: false), 7)
        XCTAssertEqual(Scrub.value(from: 7, translation: .infinity, range: nil, integer: false), 7)
    }
}

final class AvailabilityTests: XCTestCase {
    private func parameter(hidden: Bool = false, enabled: Bool = true, animated: Set<Int> = []) -> InspectorParameter {
        var parameter = InspectorParameter(name: "p", label: "P")
        parameter.hidden = hidden
        parameter.enabled = enabled
        parameter.animated = animated
        parameter.componentCount = 3
        return parameter
    }

    func testAnEnabledRowCanBeEdited() {
        let row = parameter()
        XCTAssertTrue(RowAvailability(parameter: row).canEdit(row, component: 0))
    }

    func testADisabledOrHiddenRowCannot() {
        let disabled = parameter(enabled: false)
        XCTAssertTrue(RowAvailability(parameter: disabled).isDisabled)
        XCTAssertFalse(RowAvailability(parameter: disabled).canEdit(disabled, component: 0))
        let hidden = parameter(hidden: true)
        XCTAssertTrue(RowAvailability(parameter: hidden).isHidden)
        XCTAssertFalse(RowAvailability(parameter: hidden).canEdit(hidden, component: 0))
    }

    func testAnAnimatedComponentIsReadOnlyAndItsNeighboursAreNot() {
        let row = parameter(animated: [1])
        let availability = RowAvailability(parameter: row)
        XCTAssertTrue(availability.canEdit(row, component: 0))
        XCTAssertFalse(availability.canEdit(row, component: 1))
        XCTAssertTrue(availability.canEdit(row, component: 2))
    }

    func testANodeThatCannotBeEditedDisablesEveryRow() {
        let row = parameter()
        XCTAssertTrue(RowAvailability(parameter: row, nodeEditable: false).isDisabled)
    }
}

final class TreeTests: XCTestCase {
    private func param(_ name: String, hidden: Bool = false) -> InspectorParameter {
        var parameter = InspectorParameter(name: name, label: name.capitalized)
        parameter.hidden = hidden
        return parameter
    }

    private func item(_ kind: InspectorLayoutItem.Kind, _ name: String, hidden: Bool = false, horizontal: Bool = false) -> InspectorLayoutItem {
        InspectorLayoutItem(kind: kind, name: name, label: name.capitalized, horizontal: horizontal, hidden: hidden)
    }

    func testGroupsHoldTheirMembersInOrder() {
        let layout = [
            item(.divider, "head"), item(.parameter, "a"),
            item(.groupBegin, "row", horizontal: true), item(.parameter, "b"), item(.parameter, "c"), item(.groupEnd, "row"),
            item(.parameter, "d"),
        ]
        let parameters = Dictionary(uniqueKeysWithValues: ["a", "b", "c", "d"].map { ($0, param($0)) })
        let tree = InspectorTree.build(layout: layout, parameters: parameters)
        XCTAssertEqual(tree.map(\.id), ["d:head", "p:a", "g:row", "p:d"])
        guard case .group(let group) = tree[2] else { return XCTFail("a group") }
        XCTAssertEqual(group.children.map(\.id), ["p:b", "p:c"])
        XCTAssertTrue(group.horizontal)
    }

    func testWhatIsHiddenIsLeftOutAndAnEmptyGroupWithIt() {
        let layout = [
            item(.parameter, "a"), item(.parameter, "b"),
            item(.groupBegin, "row"), item(.parameter, "c"), item(.groupEnd, "row"),
            item(.groupBegin, "gone", hidden: true), item(.parameter, "d"), item(.groupEnd, "gone"),
        ]
        var parameters = Dictionary(uniqueKeysWithValues: ["a", "b", "c", "d"].map { ($0, param($0)) })
        parameters["b"]?.hidden = true
        parameters["c"]?.hidden = true
        let tree = InspectorTree.build(layout: layout, parameters: parameters)
        XCTAssertEqual(tree.map(\.id), ["p:a"], "b is hidden, row has nothing left, gone is hidden")
    }

    func testNestedGroupsAndAParameterTheNodeDoesNotListAreHandled() {
        let layout = [
            item(.groupBegin, "outer"), item(.parameter, "a"),
            item(.groupBegin, "inner"), item(.parameter, "b"), item(.groupEnd, "inner"),
            item(.parameter, "ghost"), item(.groupEnd, "outer"),
        ]
        let parameters = ["a": param("a"), "b": param("b")]
        let tree = InspectorTree.build(layout: layout, parameters: parameters)
        guard case .group(let outer) = tree.first else { return XCTFail("a group") }
        XCTAssertEqual(outer.children.map(\.id), ["p:a", "g:inner"])
    }

    func testAStrayGroupEndDoesNotLoseTheRest() {
        let layout = [item(.groupEnd, "x"), item(.parameter, "a")]
        XCTAssertEqual(InspectorTree.build(layout: layout, parameters: ["a": param("a")]).map(\.id), ["p:a"])
    }
}

final class ContinuousEditTests: XCTestCase {
    func testADragThatChangedTheSceneRegistersOneStep() {
        var edit = ContinuousEdit()
        XCTAssertTrue(edit.begin(named: "Change Size", before: "A"))
        XCTAssertTrue(edit.isActive)
        let registration = edit.end(after: "B")
        XCTAssertEqual(registration, ContinuousEdit.Registration(name: "Change Size", before: "A"))
        XCTAssertFalse(edit.isActive)
    }

    func testADragThatCameBackRegistersNothing() {
        var edit = ContinuousEdit()
        edit.begin(named: "Change Size", before: "A")
        XCTAssertNil(edit.end(after: "A"))
    }

    func testAnEndWithNoSceneRegistersNothing() {
        var edit = ContinuousEdit()
        edit.begin(named: "Change Size", before: "A")
        XCTAssertNil(edit.end(after: nil))
        XCTAssertFalse(edit.isActive)
    }

    func testACancelledDragGivesBackTheSceneAndRegistersNothing() {
        var edit = ContinuousEdit()
        edit.begin(named: "Change Size", before: "A")
        XCTAssertEqual(edit.cancel(), "A")
        XCTAssertFalse(edit.isActive)
        XCTAssertNil(edit.end(after: "B"), "the gesture is over, so a late end is nothing")
        XCTAssertNil(edit.cancel())
    }

    func testASecondBeginIsRefusedAndTheFirstKeepsItsScene() {
        var edit = ContinuousEdit()
        XCTAssertTrue(edit.begin(named: "One", before: "A"))
        XCTAssertFalse(edit.begin(named: "Two", before: "B"))
        XCTAssertEqual(edit.end(after: "C")?.name, "One")
        XCTAssertEqual(edit.active, nil)
    }

    func testEndWithoutBeginIsNothing() {
        var edit = ContinuousEdit()
        XCTAssertNil(edit.end(after: "B"))
    }
}

final class LiveUpdateGateTests: XCTestCase {
    func testACheapSceneAppliesEveryChange() {
        var gate = LiveUpdateGate()
        XCTAssertEqual(gate.offer(now: 0), .applyNow)
        gate.applied(start: 0, end: 0.005)
        XCTAssertEqual(gate.offer(now: 0.006), .applyNow)
        gate.applied(start: 0.006, end: 0.03)
        XCTAssertEqual(gate.offer(now: 0.031), .applyNow, "24 ms is within the budget")
    }

    func testAHeavySceneHoldsChangesUntilTheCostHasPassed() {
        var gate = LiveUpdateGate()
        // Costs that are exact in binary, so the times compare exactly.
        gate.applied(start: 1.0, end: 1.125)
        XCTAssertEqual(gate.offer(now: 1.15), .holdUntil(1.25))
        XCTAssertEqual(gate.offer(now: 1.2), .holdUntil(1.25))
        XCTAssertEqual(gate.offer(now: 1.25), .applyNow)
        gate.applied(start: 1.25, end: 1.26)
        XCTAssertEqual(gate.offer(now: 1.27), .applyNow, "the scene got cheaper")
    }

    func testResetForgetsTheCost() {
        var gate = LiveUpdateGate()
        gate.applied(start: 0, end: 1)
        gate.reset()
        XCTAssertEqual(gate.offer(now: 0.1), .applyNow)
    }
}

// MARK: The model

@MainActor
private final class FakeEditing: InspectorEditing {
    var nodes: [UInt64: InspectorNode] = [:]
    var applied: [(InspectorEdit, String)] = []
    var live: [InspectorEdit] = []
    var calls: [String] = []
    var refuse: String?
    /// What the engine's work costs, so a test can make a scene heavy.
    var onLive: (() -> Void)?

    func loadInspector(node: UInt64) -> InspectorNode? { nodes[node] }
    func attributeSuggestions(node: UInt64, parameter: String) -> [String] { ["Cd", "N"] }

    func apply(_ edit: InspectorEdit, to node: UInt64, named name: String) -> EditOutcome {
        applied.append((edit, name))
        if let refuse { return .failed(refuse) }
        write(edit, node)
        return .done
    }

    func applyLive(_ edit: InspectorEdit, to node: UInt64) -> EditOutcome {
        live.append(edit)
        write(edit, node)
        onLive?()
        return .done
    }

    func beginContinuousEdit(named name: String) { calls.append("begin " + name) }
    func endContinuousEdit() { calls.append("end") }
    func setDisplay(_ node: UInt64) -> EditOutcome { calls.append("display"); return .done }

    private func write(_ edit: InspectorEdit, _ node: UInt64) {
        guard var info = nodes[node]?.parameters[edit.parameter] else { return }
        switch edit {
        case .floats(_, let values): for (index, value) in values { info.floats[index] = value }
        case .ints(_, let values): for (index, value) in values { info.ints[index] = value }
        case .string(_, let component, let value): info.strings[component] = value
        case .setExpression(_, let component, let text): info.expressions[component] = text
        case .clearExpression(_, let component): info.expressions[component] = ""
        default: break
        }
        nodes[node]?.parameters[edit.parameter] = info
    }
}

@MainActor
final class InspectorModelTests: XCTestCase {
    private var editing = FakeEditing()
    private var model = InspectorModel()
    private var time = 0.0

    override func setUp() async throws {
        editing = FakeEditing()
        model = InspectorModel()
        time = 0
        model.editing = editing
        model.clock = { [unowned self] in self.time }

        var size = InspectorParameter(name: "size", label: "Size")
        size.kind = .float
        size.componentCount = 3
        size.floats = [1, 1, 1]
        size.defaultFloats = [1, 1, 1]
        size.expressions = ["", "", ""]
        size.expressionErrors = ["", "", ""]
        size.range = InspectorRange(min: 0, max: 10, minLocked: true)
        var columns = InspectorParameter(name: "columns", label: "Columns")
        columns.kind = .int
        columns.ints = [24]
        columns.defaultInts = [24]
        columns.range = InspectorRange(min: 3, max: 64, minLocked: true)
        columns.expressions = [""]
        columns.expressionErrors = [""]
        var name = InspectorParameter(name: "name", label: "Name")
        name.kind = .string
        name.strings = ["a"]
        name.defaultStrings = ["a"]
        name.expressions = [""]
        name.expressionErrors = [""]
        var moving = InspectorParameter(name: "moving", label: "Moving")
        moving.kind = .float
        moving.floats = [1]
        moving.animated = [0]
        moving.expressions = [""]
        moving.expressionErrors = [""]
        let node = InspectorNode(
            id: 7, name: "box", path: "/box", typeLabel: "Box", isDisplay: false, error: nil,
            parameters: ["size": size, "columns": columns, "name": name, "moving": moving]
        )
        editing.nodes[7] = node
        editing.nodes[8] = InspectorNode(id: 8, name: "other", path: "/other", typeLabel: "Other", isDisplay: true, error: "broken")
    }

    func testSelectionDecidesWhatIsShown() {
        XCTAssertEqual(model.content, .none)
        model.select(nodes: [7])
        XCTAssertEqual(model.node?.name, "box")
        model.select(nodes: [7, 8])
        XCTAssertEqual(model.content, .many(2))
        model.select(nodes: [])
        XCTAssertEqual(model.content, .none)
        model.select(nodes: [99])
        XCTAssertEqual(model.content, .none, "a node that is gone shows nothing")
        model.select(nodes: [8])
        XCTAssertEqual(model.node?.error, "broken")
    }

    func testACommittedValueIsOneNamedStep() {
        model.select(nodes: [7])
        model.setFloat("size", component: 1, to: 4)
        XCTAssertEqual(editing.applied.count, 1)
        XCTAssertEqual(editing.applied[0].0, .floats(parameter: "size", values: [1: 4]))
        XCTAssertEqual(editing.applied[0].1, "Change Size")
        XCTAssertEqual(model.node?.parameters["size"]?.floats, [1, 4, 1])
    }

    func testLockedEndsHoldACommittedValueAndUnlockedOnesDoNot() {
        model.select(nodes: [7])
        model.setFloat("size", component: 0, to: -3)
        XCTAssertEqual(editing.applied.last?.0, .floats(parameter: "size", values: [0: 0]))
        model.setFloat("size", component: 0, to: 500)
        XCTAssertEqual(editing.applied.last?.0, .floats(parameter: "size", values: [0: 500]))
        model.setInt("columns", component: 0, to: 1)
        XCTAssertEqual(editing.applied.last?.0, .ints(parameter: "columns", values: [0: 3]))
    }

    func testNonFiniteValuesAreNotSent() {
        model.select(nodes: [7])
        model.setFloat("size", component: 0, to: .nan)
        model.setFloat("size", component: 0, to: .infinity)
        XCTAssertTrue(editing.applied.isEmpty)
    }

    func testAnAnimatedComponentRefusesAnEdit() {
        model.select(nodes: [7])
        model.setFloat("moving", component: 0, to: 4)
        model.setFloat("moving", component: 0, to: 4, live: true)
        XCTAssertTrue(editing.applied.isEmpty)
        XCTAssertTrue(editing.live.isEmpty)
        XCTAssertNotNil(model.rowMessages["moving"])
        model.reset("moving")
        XCTAssertTrue(editing.applied.isEmpty)
    }

    func testADragFollowsTheValueAndRegistersOnceAtTheEnd() {
        model.select(nodes: [7])
        model.beginGesture("size")
        for step in 1...5 {
            time += 0.01
            model.setFloat("size", component: 0, to: Double(step), live: true)
        }
        model.endGesture()
        XCTAssertEqual(editing.calls, ["begin Change Size", "end"])
        XCTAssertEqual(editing.live.count, 5, "a cheap scene follows every change")
        XCTAssertTrue(editing.applied.isEmpty, "no step of its own: the document registers one at the end")
        XCTAssertEqual(model.node?.parameters["size"]?.floats[0], 5)
    }

    func testAHeavySceneGetsTheNewestValueOnly() {
        model.select(nodes: [7])
        // Each change costs 100 ms of the clock, far over the budget.
        editing.onLive = { [unowned self] in self.time += 0.1 }
        model.beginGesture("size")
        for value in 2...5 {
            model.setFloat("size", component: 0, to: Double(value), live: true)
        }
        model.endGesture()
        XCTAssertEqual(editing.live.count, 2, "the first change, and the newest when the gesture ended")
        XCTAssertEqual(editing.live.first, .floats(parameter: "size", values: [0: 2]))
        XCTAssertEqual(editing.live.last, .floats(parameter: "size", values: [0: 5]))
        XCTAssertEqual(model.node?.parameters["size"]?.floats[0], 5)
    }

    func testANestedGestureIsIgnored() {
        model.select(nodes: [7])
        model.beginGesture("size")
        model.beginGesture("columns")
        model.endGesture()
        XCTAssertEqual(editing.calls, ["begin Change Size", "end"])
        model.endGesture()
        XCTAssertEqual(editing.calls.count, 2, "a second end is nothing")
    }

    func testAnExpressionDraftStartsFromTheValueAndEmptyChangesNothing() {
        model.select(nodes: [7])
        model.beginExpression("size", component: 2)
        XCTAssertEqual(model.expressionDraft?.text, "1")
        model.expressionDraft?.text = "  "
        model.commitExpression()
        XCTAssertTrue(editing.applied.isEmpty)
        XCTAssertNil(model.expressionDraft)

        model.beginExpression("size", component: 2)
        model.expressionDraft?.text = "sin($F)"
        model.commitExpression()
        XCTAssertEqual(editing.applied.last?.0, .setExpression(parameter: "size", component: 2, text: "sin($F)"))
        XCTAssertEqual(editing.applied.last?.1, "Edit Expression")
        XCTAssertEqual(model.node?.parameters["size"]?.expressions[2], "sin($F)")

        model.clearExpression("size", component: 2)
        XCTAssertEqual(editing.applied.last?.1, "Clear Expression")
    }

    func testAStringExpressionStartsQuotedAndADropdownTakesNone() {
        model.select(nodes: [7])
        model.beginExpression("name", component: 0)
        XCTAssertEqual(model.expressionDraft?.text, "\"a\"")
        model.cancelExpression()
        XCTAssertNil(model.expressionDraft)
        XCTAssertFalse(InspectorParameter(name: "d", label: "D", kind: .dropdown).takesExpression)
        XCTAssertFalse(InspectorParameter(name: "r", label: "R", kind: .ramp).takesExpression)
    }

    func testResetIsNamedAfterTheParameterAndAnUnchangedStringIsNotSent() {
        model.select(nodes: [7])
        model.reset("size")
        XCTAssertEqual(editing.applied.last?.1, "Reset Size")
        model.setString("name", to: "a")
        XCTAssertEqual(editing.applied.count, 1, "the same text is not an edit")
        model.setString("name", to: "b")
        XCTAssertEqual(editing.applied.last?.1, "Change Name")
    }

    func testARefusalIsShownOnTheRowAndClearedByTheNextGoodEdit() {
        model.select(nodes: [7])
        editing.refuse = "No."
        model.setFloat("size", component: 0, to: 2)
        XCTAssertEqual(model.rowMessages["size"], "No.")
        XCTAssertEqual(model.message, "No.")
        editing.refuse = nil
        model.setFloat("size", component: 0, to: 3)
        XCTAssertNil(model.rowMessages["size"])
        XCTAssertNil(model.message)
    }

    func testSuggestionsAreAskedForOnDemand() {
        model.select(nodes: [7])
        XCTAssertNil(model.suggestions["name"])
        model.loadSuggestions("name")
        XCTAssertEqual(model.suggestions["name"], ["Cd", "N"])
    }

    func testTheEntriesFollowTheLayout() {
        model.select(nodes: [7])
        XCTAssertTrue(model.entries.isEmpty, "this node declares no layout")
        editing.nodes[7]?.layout = [InspectorLayoutItem(kind: .parameter, name: "size")]
        model.reload()
        XCTAssertEqual(model.entries.map(\.id), ["p:size"])
    }
}

final class RampTests: XCTestCase {
    func testAddingAPointKeepsALinearCurve() {
        let points = [
            InspectorRampPoint(position: 0, value: 0, interpolation: "linear"),
            InspectorRampPoint(position: 1, value: 1, interpolation: "linear"),
        ]
        let added = RampPoints.adding(to: points)
        XCTAssertEqual(added.count, 3)
        XCTAssertEqual(added[2].position, 0.5, accuracy: 1e-12)
        XCTAssertEqual(added[2].value, 0.5, accuracy: 1e-12)
    }

    func testAddingGoesWhereTheGapIsWidest() {
        let points = [
            InspectorRampPoint(position: 0, value: 0, interpolation: "linear"),
            InspectorRampPoint(position: 0.1, value: 1, interpolation: "linear"),
            InspectorRampPoint(position: 1, value: 3, interpolation: "constant"),
        ]
        XCTAssertEqual(RampPoints.adding(to: points).last?.position ?? 0, 0.55, accuracy: 1e-12)
    }
}

final class EditorDiagnosticsTests: XCTestCase {
    private func node(error: String? = nil, warnings: [String] = []) -> EditorNode {
        EditorNode(id: 1, type: "helix::cube", name: "cube1", path: "/cube1", position: .zero, error: error, warnings: warnings)
    }

    func testANodeWithNothingToSayHasNoTooltip() {
        XCTAssertNil(node().diagnosticText)
    }

    func testTheErrorComesFirstThenTheWarnings() {
        XCTAssertEqual(node(error: "Broke", warnings: ["Odd"]).diagnosticText, "Broke\nOdd")
        XCTAssertEqual(node(warnings: ["Odd", "Odder"]).diagnosticText, "Odd\nOdder")
    }

    @MainActor
    func testTheModelAreasAreWhereTheCardsAreDrawn() {
        let model = NodeEditorModel()
        let bad = EditorNode(id: 1, type: "t", name: "bad", path: "/bad", position: CGPoint(x: 100, y: 100), error: "Broke")
        let fine = EditorNode(id: 2, type: "t", name: "fine", path: "/fine", position: CGPoint(x: 400, y: 100))
        model.reload(EditorGraph(nodes: [bad, fine], links: [], displayNode: nil, types: [:]))
        let areas = model.diagnosticAreas()
        XCTAssertEqual(areas.count, 1)
        XCTAssertEqual(areas[0].text, "Broke")
        XCTAssertTrue(areas[0].rect.contains(model.transform.viewPoint(CGPoint(x: 100, y: 100))))
    }
}

final class ExpressionErrorTextTests: XCTestCase {
    func testOnlyTheFirstLineWithoutItsCodeIsKept() {
        let raw = "error[30151]: syntax error, unexpected ')'\nexpression.das:6:20\n    return float(2 +)\n ^\n"
        XCTAssertEqual(ExpressionErrorText.short(raw), "syntax error, unexpected ')'")
        XCTAssertEqual(ExpressionErrorText.short("plain"), "plain")
        XCTAssertLessThanOrEqual(ExpressionErrorText.short(String(repeating: "x", count: 500)).count, 161)
    }
}
