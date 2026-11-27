import XCTest

/// The rules behind the first usability pass: the camera that stays where the person put it, the
/// size an edit may ask for, the engine's messages in French, the question about a second scene,
/// and the colours of selection and errors. They need no engine and no window.

// MARK: The camera

final class FramingPolicyTests: XCTestCase {
    func testTheFirstNodeFramesTheGeometry() {
        var policy = FramingPolicy()
        // Nothing was shown, and the first node makes something: no one asked for a frame.
        XCTAssertTrue(policy.shouldFrame(hasGeometry: true, requested: false))
    }

    func testAnEditLeavesTheCameraWhereThePersonPutIt() {
        var policy = FramingPolicy()
        XCTAssertTrue(policy.shouldFrame(hasGeometry: true, requested: false))
        // A slider, a link, another node, a new display node: the geometry is still there.
        for _ in 0..<5 {
            XCTAssertFalse(policy.shouldFrame(hasGeometry: true, requested: false))
        }
    }

    func testAnExplicitRequestAlwaysFrames() {
        var policy = FramingPolicy()
        _ = policy.shouldFrame(hasGeometry: true, requested: false)
        // An import, an example, the Frame command.
        XCTAssertTrue(policy.shouldFrame(hasGeometry: true, requested: true))
        XCTAssertFalse(policy.shouldFrame(hasGeometry: true, requested: false))
    }

    func testGeometryThatComesBackAfterNothingFramesAgain() {
        var policy = FramingPolicy()
        _ = policy.shouldFrame(hasGeometry: true, requested: false)
        // The only node is deleted: nothing to look at, and nothing to frame.
        XCTAssertFalse(policy.shouldFrame(hasGeometry: false, requested: false))
        // A node is made again: it is brought into view.
        XCTAssertTrue(policy.shouldFrame(hasGeometry: true, requested: false))
    }

    func testAFailureThatIsFixedFramesLikeAFirstNode() {
        var policy = FramingPolicy()
        _ = policy.shouldFrame(hasGeometry: true, requested: false)
        XCTAssertFalse(policy.shouldFrame(hasGeometry: false, requested: false))
        XCTAssertFalse(policy.shouldFrame(hasGeometry: false, requested: false))
        XCTAssertTrue(policy.shouldFrame(hasGeometry: true, requested: false))
    }

    func testAnOpenedSceneFramesOnItsFirstCook() {
        var policy = FramingPolicy()
        XCTAssertTrue(policy.shouldFrame(hasGeometry: true, requested: false))
        // An empty scene has nothing to frame, even when asked: the request is kept for the view.
        var empty = FramingPolicy()
        XCTAssertFalse(empty.shouldFrame(hasGeometry: false, requested: false))
    }
}

// MARK: The size of a geometry

final class GeometryLimitsTests: XCTestCase {
    private func estimate(_ type: String, _ values: [String: Double]) -> Double? {
        GeometryLimits.estimatedPoints(type: type) { values[$0] }
    }

    func testAGridMakesRowsTimesColumnsPoints() {
        XCTAssertEqual(estimate("helix::grid", ["rows": 4000, "columns": 4000]), 16_000_000)
        XCTAssertEqual(estimate("helix::grid", ["rows": 10, "columns": 10]), 100)
    }

    func testTheOtherGeneratorsSayWhatTheyMake() {
        XCTAssertEqual(estimate("helix::sphere", ["rows": 8, "columns": 16]), 128)
        XCTAssertEqual(estimate("helix::cylinder", ["rows": 1, "columns": 12]), 12)
        XCTAssertEqual(estimate("helix::torus", ["rows": 12, "columns": 24]), 288)
        XCTAssertEqual(estimate("helix::circle", ["divisions": 10]), 10)
        XCTAssertEqual(estimate("helix::line", ["points": 2]), 2)
        XCTAssertEqual(estimate("helix::spiral", ["turns": 3, "pointsPerTurn": 24]), 72)
    }

    func testANodeWhoseSizeFollowsItsInputHasNoEstimate() {
        XCTAssertNil(estimate("helix::transform", ["rows": 4000]))
        XCTAssertNil(estimate("helix::geometryImport", [:]))
        // A parameter the node does not hold gives nothing to go on.
        XCTAssertNil(estimate("helix::grid", ["rows": 10]))
    }

    func testANegativeValueMakesNothing() {
        XCTAssertEqual(estimate("helix::grid", ["rows": -5, "columns": 4000]), 0)
    }

    func testAnEditToAHugeGridAsksFirst() {
        // Typing 4000 into Rows of a grid that has 4000 columns already.
        let asked = GeometryLimits.largeEstimate(type: "helix::grid", overrides: ["rows": 4000]) { ["rows": 10, "columns": 4000][$0] }
        XCTAssertEqual(asked, 16_000_000)
        XCTAssertEqual(GeometryLimits.millions(try XCTUnwrap(asked)), 16)
    }

    func testAnEditBelowTheThresholdIsLeftAlone() {
        XCTAssertNil(GeometryLimits.largeEstimate(type: "helix::grid", overrides: ["rows": 2000]) { ["rows": 10, "columns": 2000][$0] })
        // 5 000 000 exactly is not above it.
        XCTAssertNil(GeometryLimits.largeEstimate(type: "helix::grid", overrides: ["rows": 5000]) { ["rows": 10, "columns": 1000][$0] })
        XCTAssertNotNil(GeometryLimits.largeEstimate(type: "helix::grid", overrides: ["rows": 5001]) { ["rows": 10, "columns": 1000][$0] })
    }

    func testAnEditThatBringsAHugeGeometryDownIsNeverHeldBack() {
        // From 16 million to 9 million is still large, and smaller: no question.
        let values = ["rows": 4000.0, "columns": 4000.0]
        XCTAssertNil(GeometryLimits.largeEstimate(type: "helix::grid", overrides: ["rows": 3000]) { values[$0] })
        // Making a huge one still larger asks.
        XCTAssertNotNil(GeometryLimits.largeEstimate(type: "helix::grid", overrides: ["rows": 5000]) { values[$0] })
    }

    func testAGeometryIsHeavyAboveFiveMillionPoints() {
        XCTAssertFalse(GeometryLimits.isHeavy(pointCount: 0))
        XCTAssertFalse(GeometryLimits.isHeavy(pointCount: 2_000_000))
        XCTAssertFalse(GeometryLimits.isHeavy(pointCount: 5_000_000))
        XCTAssertTrue(GeometryLimits.isHeavy(pointCount: 5_000_001))
        XCTAssertTrue(GeometryLimits.isHeavy(pointCount: 16_000_000))
    }

    func testMillionsAreRoundedToWholeOnes() {
        XCTAssertEqual(GeometryLimits.millions(16_000_000), 16)
        XCTAssertEqual(GeometryLimits.millions(5_400_000), 5)
        XCTAssertEqual(GeometryLimits.millions(5_600_000), 6)
    }
}

// MARK: The messages of the engine

final class EngineMessagesTests: XCTestCase {
    private static let helix = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
    private static let root = helix.deletingLastPathComponent().deletingLastPathComponent()

    /// The French of the "Errors" table, read as the file, as a lookup.
    private func french() throws -> EngineMessages.Lookup {
        try lookup(language: "fr")
    }

    private func lookup(language: String) throws -> EngineMessages.Lookup {
        let url = Self.helix.appendingPathComponent("Resources/Errors.xcstrings")
        let root = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any])
        let strings = try XCTUnwrap(root["strings"] as? [String: [String: Any]])
        var table: [String: String] = [:]
        for (key, entry) in strings {
            let localizations = entry["localizations"] as? [String: Any]
            let value = ((localizations?[language] as? [String: Any])?["stringUnit"] as? [String: Any])?["value"] as? String
            // A text equal to its key is the English: no translation, as the bundle says.
            if let value, value != key { table[key] = value }
        }
        let frozen = table
        return { frozen[$0] }
    }

    private func said(_ message: String) throws -> String {
        EngineMessages.localized(message, lookup: try french())
    }

    func testEveryMessageOfTheTableHasItsFrenchAndNothingElseIsInTheCatalog() throws {
        let url = Self.helix.appendingPathComponent("Resources/Errors.xcstrings")
        let root = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any])
        let strings = try XCTUnwrap(root["strings"] as? [String: [String: Any]])
        XCTAssertEqual(Set(strings.keys), Set(EngineMessages.keys))
        let fr = try french()
        for key in EngineMessages.keys {
            XCTAssertNotNil(fr(key), "no French for \(key.debugDescription)")
        }
        XCTAssertEqual(Set(EngineMessages.templates).count, EngineMessages.templates.count, "a message is in the table twice")
    }

    /// Messages as the engine writes them, with real names and numbers in them.
    func testEachKindOfMessageReadsInFrenchWithItsNamesAndNumbers() throws {
        let nbsp = "\u{00A0}"
        let cases: [(String, String)] = [
            ("The attribute needs a name.", "L’attribut a besoin d’un nom."),
            ("Unknown attribute type.", "Type d’attribut inconnu."),
            ("The attribute Cd can't be written as this type.", "L’attribut Cd ne peut pas être écrit avec ce type."),
            ("The attribute N can't hold vector2 values.", "L’attribut N ne peut pas contenir de valeurs de type vector2."),
            ("Noise along vector needs a point attribute named dir.", "Le bruit le long d’un vecteur a besoin d’un attribut de point nommé dir."),
            ("The skeleton has no clip named 'Run'. It has: Idle, Walk.", "Le squelette n’a pas de clip nommé «\(nbsp)Run\(nbsp)». Clips disponibles\(nbsp): Idle, Walk."),
            ("Point 12 is skinned to joint 40, but the skeleton has 30 joints.", "Le point 12 est lié à l’articulation 40, mais le squelette n’a que 30 articulations."),
            ("Sweep needs a profile of at least two points.", "Le balayage a besoin d’un profil d’au moins deux points."),
            ("The link would make the node depend on itself.", "Ce lien ferait dépendre le nœud de lui-même."),
            ("A node named 'cube1' already exists.", "Un nœud nommé «\(nbsp)cube1\(nbsp)» existe déjà."),
            ("There is no node 42.", "Il n’y a pas de nœud 42."),
            ("The node grid1 has no input 3.", "Le nœud grid1 n’a pas d’entrée 3."),
            ("Couldn't open /Users/moi/Bureau/modele-deplace.glb: there is no such file.", "Impossible d’ouvrir /Users/moi/Bureau/modele-deplace.glb\(nbsp): ce fichier n’existe pas."),
            ("/Users/moi/a.fbx is empty.", "/Users/moi/a.fbx est vide."),
            ("Files of type .xyz cannot be imported. Helix reads .glb, .gltf and .fbx.", "Les fichiers de type .xyz ne peuvent pas être importés. Helix lit .glb, .gltf and .fbx."),
            ("The range ends after it starts: the last frame is above the first.", "La plage doit finir après avoir commencé\(nbsp): la dernière image doit dépasser la première."),
            ("A ramp has between 2 and 16 points, not 40.", "Une rampe a entre 2 et 16 points, pas 40."),
            ("The parameter 'rows' holds keyframes, and the timeline would overwrite an expression.", "Le paramètre «\(nbsp)rows\(nbsp)» porte des images clés, et la chronologie écraserait une expression."),
            ("The folder /tmp/x does not exist.", "Le dossier /tmp/x n’existe pas."),
            ("The node has no parameter named 'banana'.", "Le nœud n’a pas de paramètre nommé «\(nbsp)banana\(nbsp)»."),
        ]
        for (english, expected) in cases {
            XCTAssertEqual(try said(english), expected, english)
        }
    }

    func testTheErrorsOfAnExpressionReadInFrench() throws {
        let nbsp = "\u{00A0}"
        XCTAssertEqual(try said("can't locate variable 'banana'"), "Nom inconnu\(nbsp): «\(nbsp)banana\(nbsp)»")
        XCTAssertEqual(try said("syntax error, unexpected ')'"), "Expression incomplète ou mal écrite\(nbsp): ')' inattendu")
        XCTAssertEqual(try said("syntax error, unexpected name, expecting '{'"), "Expression mal écrite\(nbsp): name inattendu, '{' attendu")
        XCTAssertEqual(try said("no matching functions or generics: foo(int const)"), "Fonction inconnue, ou mauvais arguments\(nbsp): foo(int const)")
    }

    func testTheNodeThatFailedStaysNamedInFrontOfItsMessage() throws {
        XCTAssertEqual(try said("/bruit: The attribute needs a name."), "/bruit\u{00A0}: L’attribut a besoin d’un nom.")
        XCTAssertEqual(try said("/geo/sk: Connect the posed skeleton to the second input: the output of a Skeleton Pose node."),
                       "/geo/sk\u{00A0}: Branchez le squelette posé à la seconde entrée\u{00A0}: la sortie d’un nœud Pose de squelette.")
    }

    func testAMessageThatIsNotInTheTablePassesThroughUnchanged() throws {
        for message in [
            "Something nobody thought of.",
            "/bruit: Something nobody thought of.",
            "error[30151]: syntax error",
            "",
            "The parameter 'rows' is not a float.",
        ] {
            XCTAssertEqual(try said(message), message)
        }
    }

    func testInEnglishTheMessagesAreLeftAsTheyAre() throws {
        let english = try lookup(language: "en")
        for message in ["The attribute needs a name.", "/bruit: The attribute needs a name.", "can't locate variable 'banana'"] {
            XCTAssertEqual(EngineMessages.localized(message, lookup: english), message)
        }
    }

    func testAMessageWithRegularExpressionCharactersIsMatchedAsText() throws {
        XCTAssertEqual(try said("There is no scene file at /tmp/a(b)[c].kut."), "Il n’y a pas de fichier de scène à l’emplacement /tmp/a(b)[c].kut.")
        XCTAssertEqual(try said("The attribute needs a name?"), "The attribute needs a name?")
    }

    // The sources of the engine are the truth about what it says: every message a node, the
    // importer or the facade writes as one plain text is in the table, so a new one that is
    // added without its translation fails here.
    func testEveryPlainMessageOfTheEngineSourcesIsInTheTable() throws {
        let folders = ["nodes", "src/HelixCore", "src/Engine/Import"].map { Self.root.appendingPathComponent($0) }
        let pattern = try NSRegularExpression(
            pattern: #"(?:throwError|failure|ImportError)\(\s*"((?:[^"\\]|\\.)*)"\s*\)"#
        )
        var checked = 0
        var unknown: [String] = []
        for folder in folders {
            guard let walker = FileManager.default.enumerator(at: folder, includingPropertiesForKeys: nil) else {
                throw XCTSkip("the engine sources are not next to the app")
            }
            for case let file as URL in walker where file.pathExtension == "cpp" {
                let text = try String(contentsOf: file, encoding: .utf8)
                for match in pattern.matches(in: text, range: NSRange(text.startIndex..., in: text)) {
                    guard let range = Range(match.range(at: 1), in: text) else { continue }
                    let literal = String(text[range]).replacingOccurrences(of: "\\\"", with: "\"").replacingOccurrences(of: "\\'", with: "'")
                    checked += 1
                    if !EngineMessages.isKnown(literal) { unknown.append("\(file.lastPathComponent): \(literal)") }
                }
            }
        }
        XCTAssertGreaterThan(checked, 40, "the scan found too few messages: did the sources move?")
        XCTAssertEqual(unknown, [], "messages of the engine with no entry in EngineMessages.templates")
    }
}

// MARK: Scenes

final class SceneSwitchTests: XCTestCase {
    func testASceneIsNamedByItsFileAndAModelByItsFullName() {
        XCTAssertEqual(SceneNames.name(for: URL(fileURLWithPath: "/a/Grand graphe.kut"), untitled: "Untitled"), "Grand graphe")
        XCTAssertEqual(SceneNames.name(for: URL(fileURLWithPath: "/a/Knight.glb"), untitled: "Untitled"), "Knight.glb")
        XCTAssertEqual(SceneNames.name(for: URL(fileURLWithPath: "/a/SCENE.KUT"), untitled: "Untitled"), "SCENE")
        XCTAssertEqual(SceneNames.name(for: nil, untitled: "Sans titre"), "Sans titre")
    }

    func testTheQuestionNamesBothScenes() {
        let request = SceneSwitchRequest(currentName: "A", newName: "B")
        XCTAssertEqual(request.title, "Close “A” and open “B”?")
        XCTAssertNotEqual(SceneSwitchRequest(currentName: "A", newName: "B"), request, "each question is its own")
    }
}

// MARK: The legend of the viewport

final class NavigationLegendTests: XCTestCase {
    func testTheLegendIsShownUntilItIsHiddenAndTheChoiceIsKept() throws {
        let defaults = try XCTUnwrap(UserDefaults(suiteName: "NavigationLegendTests-\(UUID().uuidString)"))
        XCTAssertFalse(NavigationLegendSetting.isHidden(in: defaults))
        NavigationLegendSetting.setHidden(true, in: defaults)
        XCTAssertTrue(NavigationLegendSetting.isHidden(in: defaults))
        // Help > Show Navigation Help brings it back.
        NavigationLegendSetting.setHidden(false, in: defaults)
        XCTAssertFalse(NavigationLegendSetting.isHidden(in: defaults))
    }

    func testTheLegendListsWhatTheViewportReallyDoes() {
        let gestures = NavigationBindings.rows.map(\.gesture)
        // The keys HelixMetalView handles: orbit, pan with option, zoom with control or the wheel,
        // the scroll with shift and command, a pinch, and F.
        XCTAssertTrue(gestures.contains("Drag"))
        XCTAssertTrue(gestures.contains { $0.contains("⌥") })
        XCTAssertTrue(gestures.contains { $0.contains("⌃") && $0.contains("wheel") })
        XCTAssertTrue(gestures.contains { $0.contains("⇧") })
        XCTAssertTrue(gestures.contains { $0.contains("⌘") && $0.contains("pinch") })
        XCTAssertTrue(gestures.contains("F"))
        XCTAssertEqual(Set(gestures).count, gestures.count)
    }
}

// MARK: Colours

final class AccentColorTests: XCTestCase {
    private func components(of fileName: String, dark: Bool) throws -> RGB {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("Resources/Assets.xcassets/AccentColor.colorset/Contents.json")
        let root = try XCTUnwrap(JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any])
        let colors = try XCTUnwrap(root["colors"] as? [[String: Any]])
        let entry = try XCTUnwrap(colors.first { ($0["appearances"] != nil) == dark })
        let values = try XCTUnwrap((entry["color"] as? [String: Any])?["components"] as? [String: String])
        func channel(_ name: String) throws -> Double {
            let text = try XCTUnwrap(values[name])
            return Double(try XCTUnwrap(UInt8(text.dropFirst(2), radix: 16))) / 255
        }
        return RGB(red: try channel("red"), green: try channel("green"), blue: try channel("blue"))
    }

    func testTheAccentOfTheAppAssetIsTheOneOfTheSystemThemes() throws {
        XCTAssertEqual(try components(of: "AccentColor", dark: false).hexString, ThemePalette.light.accent.hexString)
        XCTAssertEqual(try components(of: "AccentColor", dark: true).hexString, ThemePalette.dark.accent.hexString)
    }
}
