import XCTest

/// The file parameters of the inspector (what a node that reads a file shows), the buttons at the
/// end of a row, and the help of the expression editor, as values: none of it needs an engine.
@MainActor
final class FileParameterTests: XCTestCase {
    // MARK: The file as it is shown

    func testAFileIsShownByItsNameWithTheFolderApart() {
        let display = FileDisplay(path: "/Volumes/Work/Models/White Knight.glb")
        XCTAssertEqual(display.name, "White Knight.glb")
        XCTAssertEqual(display.folder, "/Volumes/Work/Models")
    }

    func testTheHomeFolderIsWrittenAsATilde() {
        let display = FileDisplay(path: NSHomeDirectory() + "/Desktop/knight.glb")
        XCTAssertEqual(display.name, "knight.glb")
        XCTAssertEqual(display.folder, "~/Desktop")
    }

    func testAPathWithSpacesAroundItIsRead() {
        XCTAssertEqual(FileDisplay(path: "  /a/b/c.obj \n").name, "c.obj")
        XCTAssertEqual(FileDisplay(path: "c.obj").folder, "")
    }

    func testThePanelOpensInTheNearestFolderThatStillExists() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("helix-file-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: root.appendingPathComponent("a"), withIntermediateDirectories: true)
        addTeardownBlock { try? FileManager.default.removeItem(at: root) }
        // Its own folder when it is there, otherwise the nearest one above.
        XCTAssertEqual(FileDisplay.nearestExistingFolder(of: root.path + "/a/gone.glb"), root.path + "/a")
        XCTAssertEqual(FileDisplay.nearestExistingFolder(of: root.path + "/a/b/c/gone.glb"), root.path + "/a")
        XCTAssertNil(FileDisplay.nearestExistingFolder(of: ""))
    }

    // MARK: The parameters of a node that reads a file

    private func importNode(path: String, blender: String = "") -> InspectorNode {
        var filePath = InspectorParameter(name: "filePath", label: "File Path")
        filePath.kind = .string
        filePath.style = .file
        filePath.componentCount = 1
        filePath.strings = [path]
        var blenderPath = InspectorParameter(name: "blenderPath", label: "Blender")
        blenderPath.kind = .string
        blenderPath.strings = [blender]
        return InspectorNode(
            id: 1, name: "knight", path: "/knight", type: "helix::geometryImport", typeLabel: "Geometry Import", isDisplay: true,
            layout: [
                InspectorLayoutItem(kind: .parameter, name: "filePath"),
                InspectorLayoutItem(kind: .parameter, name: "blenderPath"),
            ],
            parameters: ["filePath": filePath, "blenderPath": blenderPath]
        )
    }

    func testTheFileStyleIsReadFromTheNodeFile() {
        XCTAssertEqual(InspectorStyle(name: "file"), .file)
        XCTAssertEqual(InspectorStyle(name: "plain-text"), .plain)
    }

    func testTheFileOfANodeIsTheParameterOfTheFileStyle() {
        let node = importNode(path: " /a/b.glb ")
        XCTAssertEqual(node.fileParameters.map(\.name), ["filePath"])
        XCTAssertEqual(node.filePath, "/a/b.glb")
        XCTAssertNil(importNode(path: "  ").filePath)
    }

    func testBlenderIsAskedOnlyForABlendFile() {
        // Through the model, as the panel reads the rows.
        func shown(_ node: InspectorNode) -> [String] {
            let model = InspectorModel()
            let editing = Editing(node)
            model.editing = editing
            model.select(nodes: [1])
            return withExtendedLifetime(editing) { model.entries }.compactMap {
                if case .parameter(let parameter) = $0 { parameter.name } else { nil }
            }
        }
        XCTAssertEqual(shown(importNode(path: "/a/b.glb")), ["filePath"])
        XCTAssertEqual(shown(importNode(path: "")), ["filePath"])
        XCTAssertEqual(shown(importNode(path: "/a/b.blend")), ["filePath", "blenderPath"])
        XCTAssertEqual(shown(importNode(path: "/a/B.BLEND")), ["filePath", "blenderPath"])
        // A folder called .blend is not a Blender file.
        XCTAssertEqual(shown(importNode(path: "/a.blend/b.glb")), ["filePath"])
    }

    func testTheBlenderRuleIsOnlyForTheImport() {
        var node = importNode(path: "/a/b.glb")
        node.type = "helix::cube"
        XCTAssertFalse(node.isHiddenByRule("blenderPath"))
        XCTAssertFalse(importNode(path: "/a/b.glb").isHiddenByRule("filePath"))
    }

    // MARK: Choosing a file, reading it again

    @MainActor
    private final class Editing: InspectorEditing {
        var node: InspectorNode
        var applied: [(InspectorEdit, String)] = []
        var reloaded: [UInt64] = []
        var reloadOutcome = EditOutcome.done
        init(_ node: InspectorNode) { self.node = node }
        func loadInspector(node: UInt64) -> InspectorNode? { self.node }
        func attributeSuggestions(node: UInt64, parameter: String) -> [String] { [] }
        func apply(_ edit: InspectorEdit, to node: UInt64, named name: String) -> EditOutcome {
            applied.append((edit, name))
            if case .string(let parameter, _, let value) = edit { self.node.parameters[parameter]?.strings = [value] }
            return .done
        }
        func applyLive(_ edit: InspectorEdit, to node: UInt64) -> EditOutcome { .done }
        func beginContinuousEdit(named name: String) {}
        func endContinuousEdit() {}
        func setDisplay(_ node: UInt64) -> EditOutcome { .done }
        func rename(_ node: UInt64, to name: String) -> EditOutcome { .done }
        func reloadFile(node: UInt64) -> EditOutcome { reloaded.append(node); return reloadOutcome }
    }

    func testLocatingAFileIsOneEditOfTheFilePath() throws {
        let editing = Editing(importNode(path: "/old/knight.glb"))
        let model = InspectorModel()
        model.editing = editing
        model.select(nodes: [1])
        model.setString("filePath", to: "/new/knight.glb")
        XCTAssertEqual(editing.applied.count, 1)
        XCTAssertEqual(editing.applied.first?.0, .string(parameter: "filePath", component: 0, value: "/new/knight.glb"))
        XCTAssertEqual(model.node?.filePath, "/new/knight.glb")
    }

    func testReloadingAsksTheDocumentToReadTheFileAgain() {
        let editing = Editing(importNode(path: "/a/b.glb"))
        let model = InspectorModel()
        model.editing = editing
        model.select(nodes: [1])
        model.reloadFile()
        XCTAssertEqual(editing.reloaded, [1])
        // It is not an edit of the scene: nothing for undo.
        XCTAssertTrue(editing.applied.isEmpty)
    }

    func testAReloadThatFailsSaysWhy() {
        let editing = Editing(importNode(path: "/a/b.glb"))
        editing.reloadOutcome = .failed("It did not work.")
        let model = InspectorModel()
        model.editing = editing
        model.select(nodes: [1])
        model.reloadFile()
        XCTAssertEqual(model.message, "It did not work.")
    }

    // MARK: Help, keys and reset

    func testARowHasHelpWhenItHasATooltipOrADocumentation() {
        var parameter = InspectorParameter(name: "gain", label: "Gain")
        XCTAssertFalse(parameter.hasHelp)
        parameter.tooltip = "  How much each octave counts.  "
        XCTAssertEqual(parameter.helpParagraphs, ["How much each octave counts."])
        parameter.documentation = "Lower values make the fine detail fade."
        XCTAssertEqual(parameter.helpParagraphs.count, 2)
        parameter.tooltip = ""
        XCTAssertEqual(parameter.helpParagraphs, ["Lower values make the fine detail fade."])
    }

    private func float(_ values: [Double], defaults: [Double]) -> InspectorParameter {
        var parameter = InspectorParameter(name: "t", label: "Translate")
        parameter.kind = .float
        parameter.componentCount = values.count
        parameter.floats = values
        parameter.defaultFloats = defaults
        return parameter
    }

    func testOnlyAFloatCanBeKeyedAndOnlyAValueCanBeReset() {
        XCTAssertTrue(float([0], defaults: [0]).canKeyframe)
        var integer = InspectorParameter(name: "n", label: "N")
        integer.kind = .int
        XCTAssertFalse(integer.canKeyframe)
        XCTAssertTrue(integer.canReset)
        var divider = InspectorParameter(name: "d", label: "D")
        divider.kind = .other
        XCTAssertFalse(divider.canReset)
        // A path or a block of code is not emptied by an arrow beside its label.
        var path = InspectorParameter(name: "p", label: "P")
        path.kind = .string
        path.style = .file
        XCTAssertFalse(path.canReset)
        path.style = .code
        XCTAssertFalse(path.canReset)
    }

    func testTheResetShowsOnlyForAValueThatIsNotTheDefault() {
        XCTAssertFalse(float([0, 0, 0], defaults: [0, 0, 0]).wantsResetShown)
        XCTAssertTrue(float([0, 2, 0], defaults: [0, 0, 0]).wantsResetShown)
        var expression = float([0], defaults: [0])
        expression.expressions = ["$F"]
        XCTAssertTrue(expression.wantsResetShown)
        // A ramp is compared by the engine, so its arrow waits for the pointer.
        var ramp = InspectorParameter(name: "r", label: "R")
        ramp.kind = .ramp
        XCTAssertFalse(ramp.wantsResetShown)
    }

    func testTheDiamondIsFilledWhenAnyComponentHasAKeyOnThisFrame() {
        var parameter = float([1, 2, 3], defaults: [0, 0, 0])
        XCTAssertFalse(parameter.isKeyedHere)
        parameter.animated = [0, 1, 2]
        XCTAssertFalse(parameter.isKeyedHere)
        XCTAssertTrue(parameter.resetIsBlockedByKeyframes)
        parameter.keyed = [1]
        XCTAssertTrue(parameter.isKeyedHere)
    }

    func testTheDiamondKeysEveryComponentAndTakesOffTheKeysOfThisFrame() {
        var parameter = float([1, 2, 3], defaults: [0, 0, 0])
        var node = importNode(path: "")
        node.parameters["t"] = parameter
        let editing = Editing(node)
        let model = InspectorModel()
        model.editing = editing
        model.select(nodes: [1])
        model.setKeyframe("t")
        XCTAssertEqual(editing.applied.last?.0, .setKeyframe(parameter: "t", components: [0, 1, 2]))
        parameter.animated = [0, 1, 2]
        parameter.keyed = [0, 1, 2]
        editing.node.parameters["t"] = parameter
        model.reload()
        model.removeKeyframe("t")
        XCTAssertEqual(editing.applied.last?.0, .removeKeyframe(parameter: "t", components: [0, 1, 2]))
    }

    func testAResetWhileTheValueFollowsKeyframesSaysWhyAndChangesNothing() {
        var parameter = float([1], defaults: [0])
        parameter.animated = [0]
        var node = importNode(path: "")
        node.parameters["t"] = parameter
        let editing = Editing(node)
        let model = InspectorModel()
        model.editing = editing
        model.select(nodes: [1])
        model.reset("t")
        XCTAssertTrue(editing.applied.isEmpty)
        XCTAssertNotNil(model.rowMessages["t"])
    }

    // MARK: The expression help

    func testTheHelpOfExpressionsListsTheVariablesAndOneExampleEach() {
        let items = ExpressionHelp.items
        XCTAssertTrue(items.contains { $0.syntax == "$F" && $0.example.contains("$F") })
        XCTAssertTrue(items.contains { $0.syntax == "$T" && $0.example.hasPrefix("sin(") })
        XCTAssertTrue(items.contains { $0.example == "prm(\"grille1.size\")" })
        XCTAssertTrue(items.contains { $0.example == "rand($F)" })
        XCTAssertEqual(Set(items.map(\.id)).count, items.count)
        for item in items {
            XCTAssertFalse(item.summary.isEmpty, item.syntax)
            XCTAssertFalse(item.example.isEmpty, item.syntax)
        }
    }
}
