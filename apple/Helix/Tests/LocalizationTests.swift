import XCTest

/// The string catalogs, read as files: nothing here needs the app to run in French. They check
/// that every text has a French translation, that the translation takes the same arguments as
/// the English, and that what is looked up falls back to English where there is no translation.
final class LocalizationTests: XCTestCase {
    private static let helix = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
    private static let resources = helix.appendingPathComponent("Resources")

    // MARK: Reading a catalog

    private struct Entry {
        var key: String
        /// The English value of each plural category, or under "" for a text with no plural.
        var english: [String: String]
        var french: [String: String]
    }

    private func catalog(_ name: String) throws -> [Entry] {
        let url = Self.resources.appendingPathComponent(name)
        let data = try Data(contentsOf: url)
        let root = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        XCTAssertEqual(root["sourceLanguage"] as? String, "en", name)
        let strings = try XCTUnwrap(root["strings"] as? [String: Any], name)
        return try strings.keys.sorted().map { key in
            let entry = try XCTUnwrap(strings[key] as? [String: Any])
            let localizations = entry["localizations"] as? [String: Any] ?? [:]
            return Entry(key: key, english: Self.values(of: localizations["en"]), french: Self.values(of: localizations["fr"]))
        }
    }

    /// The values of one language of an entry: one under "", or one for each plural category.
    private static func values(of localization: Any?) -> [String: String] {
        guard let localization = localization as? [String: Any] else { return [:] }
        if let unit = localization["stringUnit"] as? [String: Any], let value = unit["value"] as? String {
            return ["": value]
        }
        guard let plural = (localization["variations"] as? [String: Any])?["plural"] as? [String: Any] else { return [:] }
        var result: [String: String] = [:]
        for (category, variation) in plural {
            if let unit = (variation as? [String: Any])?["stringUnit"] as? [String: Any], let value = unit["value"] as? String {
                result[category] = value
            }
        }
        return result
    }

    /// The conversions of a format string, in the order the arguments are taken: `%@`, `%lld`, and
    /// the positional `%2$@`, which count by their position.
    static func specifiers(of format: String) -> [String] {
        let pattern = #"%(?:(\d+)\$)?[-+ #0]*\d*(?:\.\d+)?(ll|l|h)?([@dDuUxXfFeEgGcCsSpaA])"#
        // swiftlint:disable:next force_try
        let expression = try! NSRegularExpression(pattern: pattern)
        let text = format.replacingOccurrences(of: "%%", with: "")
        let range = NSRange(text.startIndex..., in: text)
        var found: [(position: Int?, type: String)] = []
        for match in expression.matches(in: text, range: range) {
            func group(_ index: Int) -> String {
                Range(match.range(at: index), in: text).map { String(text[$0]) } ?? ""
            }
            found.append((Int(group(1)), group(2) + group(3)))
        }
        if found.contains(where: { $0.position != nil }) {
            found.sort { ($0.position ?? 0) < ($1.position ?? 0) }
        }
        return found.map(\.type)
    }

    // MARK: Every catalog

    private static let catalogs = ["Localizable.xcstrings", "Nodes.xcstrings", "InfoPlist.xcstrings"]

    func testEveryTextHasAFrenchTranslation() throws {
        for name in Self.catalogs {
            let entries = try catalog(name)
            XCTAssertFalse(entries.isEmpty, name)
            for entry in entries {
                XCTAssertFalse(entry.english.isEmpty, "\(name): no English for \(entry.key.debugDescription)")
                XCTAssertEqual(
                    Set(entry.french.keys), Set(entry.english.keys),
                    "\(name): the French of \(entry.key.debugDescription) does not have the plural forms of the English"
                )
                for (category, value) in entry.french {
                    XCTAssertFalse(
                        value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
                        "\(name): empty French for \(entry.key.debugDescription) \(category)"
                    )
                }
            }
        }
    }

    func testTheFrenchTakesTheArgumentsOfTheEnglish() throws {
        for name in Self.catalogs {
            for entry in try catalog(name) {
                for (category, english) in entry.english {
                    guard let french = entry.french[category] else { continue }
                    XCTAssertEqual(
                        Self.specifiers(of: french), Self.specifiers(of: english),
                        "\(name): \(entry.key.debugDescription) \(category): French \(french.debugDescription) against English \(english.debugDescription)"
                    )
                }
                // A plural form that shows no number is not an argument of the key, and is fine; the
                // key itself, the text SwiftUI looks up, carries the arguments of the English.
                if let other = entry.english["other"] ?? entry.english[""] {
                    XCTAssertEqual(
                        Self.specifiers(of: entry.key).count, Self.specifiers(of: other).count,
                        "\(name): \(entry.key.debugDescription) takes other arguments than its English"
                    )
                }
            }
        }
    }

    func testTheFrenchSpacesBeforeItsPunctuationDoNotBreakTheLine() throws {
        for name in Self.catalogs {
            for entry in try catalog(name) {
                for value in entry.french.values {
                    for mark in [" :", " ;", " ?", " !", " »", "« "] {
                        XCTAssertFalse(value.contains(mark), "\(name): \(value.debugDescription) has an ordinary space before or after a French mark")
                    }
                    XCTAssertFalse(value.contains("..."), "\(name): \(value.debugDescription) spells an ellipsis with three dots")
                }
            }
        }
    }

    func testTheCatalogsHaveNoLeftOverOfAnotherLanguage() throws {
        // An English word that a translation should have replaced, caught on the commonest ones.
        for name in Self.catalogs {
            for entry in try catalog(name) {
                for value in entry.french.values {
                    for word in [" the ", " of ", " and ", " to the "] {
                        XCTAssertFalse(value.lowercased().contains(word), "\(name): \(value.debugDescription) looks English")
                    }
                }
            }
        }
    }

    // MARK: The plurals

    func testACountOfNodesTakesItsPluralFormInBothLanguages() throws {
        let entries = try catalog("Localizable.xcstrings")
        let entry = try XCTUnwrap(entries.first { $0.key == "%lld nodes selected" })
        XCTAssertEqual(entry.english["one"], "%lld node selected")
        XCTAssertEqual(entry.english["other"], "%lld nodes selected")
        XCTAssertEqual(entry.french["one"], "%lld nœud sélectionné")
        XCTAssertEqual(entry.french["other"], "%lld nœuds sélectionnés")
    }

    // MARK: Info.plist

    func testTheTextsOfInfoPlistAreInItsCatalog() throws {
        let plist = try XCTUnwrap(
            PropertyListSerialization.propertyList(
                from: Data(contentsOf: Self.resources.appendingPathComponent("Info.plist")), format: nil
            ) as? [String: Any]
        )
        var wanted: Set<String> = []
        if plist["NSHumanReadableCopyright"] is String { wanted.insert("NSHumanReadableCopyright") }
        for type in plist["CFBundleDocumentTypes"] as? [[String: Any]] ?? [] {
            if let name = type["CFBundleTypeName"] as? String { wanted.insert(name) }
        }
        for list in ["UTExportedTypeDeclarations", "UTImportedTypeDeclarations"] {
            for type in plist[list] as? [[String: Any]] ?? [] {
                if let description = type["UTTypeDescription"] as? String { wanted.insert(description) }
            }
        }
        XCTAssertGreaterThan(wanted.count, 10)
        let keys = Set(try catalog("InfoPlist.xcstrings").map(\.key))
        XCTAssertEqual(wanted.subtracting(keys), [], "texts of Info.plist with no entry in InfoPlist.xcstrings")
    }

    // MARK: The node strings follow the node files

    func testTheNodeStringsAreUpToDateWithTheNodeFiles() throws {
        let python = ["/usr/bin/python3", "/opt/homebrew/bin/python3", "/usr/local/bin/python3"]
            .first { FileManager.default.isExecutableFile(atPath: $0) }
        let script = Self.helix.appendingPathComponent("Scripts/extract-node-strings.py")
        let nodes = Self.helix.deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent("nodes")
        guard let python, FileManager.default.fileExists(atPath: nodes.path) else {
            throw XCTSkip("no Python, or no node files, to check the strings against")
        }
        let process = Process()
        process.executableURL = URL(fileURLWithPath: python)
        process.arguments = ["-I", script.path, "--check"]
        let output = Pipe()
        process.standardOutput = output
        process.standardError = output
        try process.run()
        process.waitUntilExit()
        let text = String(data: output.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
        XCTAssertEqual(process.terminationStatus, 0, "Nodes.xcstrings is out of date or untranslated; run Scripts/extract-node-strings.py\n\(text)")
    }

    func testTheNodeKeysAreTheOnesTheCodeLooksUp() throws {
        let entries = try catalog("Nodes.xcstrings")
        let keys = Set(entries.map(\.key))
        XCTAssertTrue(keys.contains("node.helix::cube.label"))
        XCTAssertTrue(keys.contains("param.helix::sweep.profileShape.label"))
        XCTAssertTrue(keys.contains("option.helix::sweep.profileShape.round"))
        XCTAssertTrue(keys.contains("tag.curve"))
        let cube = try XCTUnwrap(entries.first { $0.key == "node.helix::cube.label" })
        XCTAssertEqual(cube.english[""], "Cube")
        XCTAssertEqual(cube.french[""], "Cube")
        let sweep = try XCTUnwrap(entries.first { $0.key == "node.helix::sweep.label" })
        XCTAssertEqual(sweep.french[""], "Balayage")
    }

    func testTheTermsOfTheNodesHaveAnExplanationInBothLanguages() throws {
        let entries = try catalog("Nodes.xcstrings")
        let documentation = entries.filter { $0.key.hasSuffix(".documentation") }
        // The jargon the usability audit cites, and the nodes it names.
        XCTAssertGreaterThanOrEqual(documentation.count, 20)
        for key in [
            "param.helix::attributeNoise.lacunarity.documentation", "param.helix::attributeNoise.fractalType.documentation",
            "param.helix::attributeNoise.octaves.documentation", "param.helix::attributeNoise.gain.documentation",
            "param.helix::attributeNoise.weightedStrength.documentation", "param.helix::sineWave.remap.documentation",
            "param.helix::simulation.sub_steps.documentation", "param.helix::extrude.inset.documentation",
            "param.helix::sweep.endCapType.documentation", "param.helix::skeletonPose.clip.documentation",
            "param.helix::skinDeform.normals.documentation", "param.helix::geometryImport.filePath.documentation",
        ] {
            let entry = try XCTUnwrap(entries.first { $0.key == key }, key)
            XCTAssertFalse((entry.english[""] ?? "").isEmpty, key)
            XCTAssertFalse((entry.french[""] ?? "").isEmpty, key)
            XCTAssertNotEqual(entry.english[""], entry.french[""], "\(key) is not translated")
        }
    }

    func testTheWordsTheAuditFoundConfusingAreRenamed() throws {
        let entries = try catalog("Nodes.xcstrings")
        func french(_ key: String) throws -> String? { try XCTUnwrap(entries.first { $0.key == key }, key).french[""] }
        // A node is named by a noun, so that on the graph it does not read as a button.
        XCTAssertEqual(try french("node.helix::delete.label"), "Suppression")
        XCTAssertEqual(try french("node.helix::duplicate.label"), "Duplication")
        XCTAssertEqual(try french("node.helix::extrude.label"), "Extrusion")
        XCTAssertEqual(try french("node.helix::merge.label"), "Fusion")
        XCTAssertEqual(try french("node.helix::transform.label"), "Transformation")
        XCTAssertEqual(try french("node.helix::skinDeform.label"), "Déformation par squelette")
        XCTAssertEqual(try french("param.helix::extrude.inset.label"), "Retrait")
        XCTAssertEqual(try french("param.helix::sweep.caps_divider.label"), "Couvercles")
        XCTAssertEqual(try french("param.helix::cylinder.caps_divider.label"), "Couvercles")
        XCTAssertEqual(try french("param.helix::skeletonPose.clip.label"), "Animation (clip)")
        // The scale of an import says what it is, in both languages.
        XCTAssertEqual(try french("param.helix::geometryImport.size.label"), "Échelle")
        let english = try XCTUnwrap(entries.first { $0.key == "param.helix::geometryImport.size.label" }).english[""]
        XCTAssertEqual(english, "Scale")
    }

    // MARK: The lookup of node text

    /// A bundle with one table, "Nodes", in English only.
    private func makeBundle(_ strings: [String: String]) throws -> Bundle {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("helix-l10n-\(UUID().uuidString).bundle")
        let resources = root.appendingPathComponent("Contents/Resources")
        let language = resources.appendingPathComponent("en.lproj")
        try FileManager.default.createDirectory(at: language, withIntermediateDirectories: true)
        let plist: [String: Any] = ["CFBundleDevelopmentRegion": "en", "CFBundleIdentifier": "test.helix.l10n", "CFBundlePackageType": "BNDL"]
        try PropertyListSerialization.data(fromPropertyList: plist, format: .xml, options: 0)
            .write(to: root.appendingPathComponent("Contents/Info.plist"))
        try PropertyListSerialization.data(fromPropertyList: strings, format: .binary, options: 0)
            .write(to: language.appendingPathComponent("\(NodeText.table).strings"))
        addTeardownBlock { try? FileManager.default.removeItem(at: root) }
        return try XCTUnwrap(Bundle(url: root))
    }

    func testNodeTextUsesTheTableAndFallsBackToTheEnginesEnglish() throws {
        let bundle = try makeBundle([
            "node.helix::cube.label": "Cube!",
            "param.helix::cube.size.label": "Taille",
            "param.helix::cube.size.tooltip": "La taille",
            "param.helix::cube.size.documentation": "Longue",
            "param.helix::circle.arc.caption": "arc!",
            "option.helix::circle.orientation.xy": "Plan XY",
            "node.helix::line.input.0": "Entrée",
            "tag.curve": "courbe",
        ])
        XCTAssertEqual(NodeText.typeLabel("helix::cube", fallback: "Cube", in: bundle), "Cube!")
        XCTAssertEqual(NodeText.label("helix::cube", parameter: "size", fallback: "Size", in: bundle), "Taille")
        XCTAssertEqual(NodeText.tooltip("helix::cube", parameter: "size", fallback: "The size", in: bundle), "La taille")
        XCTAssertEqual(NodeText.caption("helix::circle", parameter: "arc", fallback: "arc", in: bundle), "arc!")
        XCTAssertEqual(NodeText.option("helix::circle", parameter: "orientation", value: "xy", fallback: "XY Plane", in: bundle), "Plan XY")
        XCTAssertEqual(NodeText.inputLabel("helix::line", index: 0, fallback: "Input", in: bundle), "Entrée")
        XCTAssertEqual(NodeText.tag("curve", in: bundle), "courbe")
        XCTAssertEqual(NodeText.documentation("helix::cube", parameter: "size", fallback: "Long", in: bundle), "Longue")
        XCTAssertEqual(NodeText.documentation("helix::cube", parameter: "other", fallback: "Long", in: bundle), "Long")

        // A node that is not translated yet reads as the engine gives it.
        XCTAssertEqual(NodeText.typeLabel("helix::newNode", fallback: "New Node", in: bundle), "New Node")
        XCTAssertEqual(NodeText.label("helix::cube", parameter: "other", fallback: "Other", in: bundle), "Other")
        XCTAssertEqual(NodeText.tooltip("helix::cube", parameter: "other", fallback: "", in: bundle), "")
        XCTAssertEqual(NodeText.option("helix::circle", parameter: "orientation", value: "zx", fallback: "ZX Plane", in: bundle), "ZX Plane")
        XCTAssertEqual(NodeText.tag("surface", in: bundle), "surface")
    }

    func testRampInterpolationsAreNamedAndUnknownOnesKeepTheEnginesName() {
        XCTAssertEqual(RampText.interpolation("linear", fallback: "x"), "Linear")
        XCTAssertEqual(RampText.interpolation("constant", fallback: "x"), "Constant")
        XCTAssertEqual(RampText.interpolation("bspline", fallback: "x"), "B Spline")
        XCTAssertEqual(RampText.interpolation("cubic", fallback: "Cubic"), "Cubic")
    }

    // MARK: The palette finds a node by either language

    func testThePaletteSearchesTheEnglishNameAndTheTranslatedOne() {
        let camera = EditorNodeType(
            name: "helix::camera", label: "Caméra", tags: ["caméra", "scène"],
            searchLabels: ["Camera"], searchTags: ["camera", "scene"]
        )
        let noise = EditorNodeType(
            name: "helix::attributeNoise", label: "Bruit d’attribut", tags: ["attribut", "bruit"],
            searchLabels: ["Attribute Noise"], searchTags: ["attribute", "noise"]
        )
        let types = [camera, noise]
        func names(_ query: String) -> [String] { NodeSearch.rank(query, in: types).map(\.type.name) }
        XCTAssertEqual(names("camera"), ["helix::camera"])
        XCTAssertEqual(names("caméra"), ["helix::camera"])
        XCTAssertEqual(names("CAMERA"), ["helix::camera"])
        XCTAssertEqual(names("bruit"), ["helix::attributeNoise"])
        XCTAssertEqual(names("noise"), ["helix::attributeNoise"])
        XCTAssertEqual(names("attrn"), ["helix::attributeNoise"])
        XCTAssertEqual(names("scene"), ["helix::camera"])
        XCTAssertEqual(names("scène"), ["helix::camera"])
        XCTAssertEqual(names("zzz"), [])
    }

    func testThePaletteIgnoresAccentsInTheQueryAndInTheLabel() {
        let helice = EditorNodeType(name: "helix::spiral", label: "Hélice", tags: ["hélice"])
        let accented = EditorNodeType(name: "helix::sphere", label: "Sphère")
        func names(_ query: String) -> [String] { NodeSearch.rank(query, in: [helice, accented]).map(\.type.name) }
        XCTAssertEqual(names("helice"), ["helix::spiral"])
        XCTAssertEqual(names("hélice"), ["helix::spiral"])
        XCTAssertEqual(names("sphere"), ["helix::sphere"])
        XCTAssertEqual(names("SPHÈRE"), ["helix::sphere"])
    }

    // MARK: The language choice

    private func freshDefaults() -> (UserDefaults, String) {
        let suite = "helix.l10n.tests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        addTeardownBlock { UserDefaults(suiteName: suite)?.removePersistentDomain(forName: suite) }
        return (defaults, suite)
    }

    func testTheLanguageChoiceIsTheAppleLanguagesOverrideAndSystemRemovesIt() {
        let (defaults, domain) = freshDefaults()
        XCTAssertEqual(AppLanguage.stored(in: defaults, domain: domain), .system)

        AppLanguage.french.store(in: defaults)
        XCTAssertEqual(defaults.array(forKey: "AppleLanguages") as? [String], ["fr"])
        XCTAssertEqual(AppLanguage.stored(in: defaults, domain: domain), .french)

        AppLanguage.english.store(in: defaults)
        XCTAssertEqual(AppLanguage.stored(in: defaults, domain: domain), .english)

        AppLanguage.system.store(in: defaults)
        XCTAssertNil(defaults.persistentDomain(forName: domain)?["AppleLanguages"])
        XCTAssertEqual(AppLanguage.stored(in: defaults, domain: domain), .system)
    }

    func testAnOverrideOfAnotherLanguageReadsAsTheSystemAndARegionalOneAsItsLanguage() {
        let (defaults, domain) = freshDefaults()
        defaults.set(["de"], forKey: "AppleLanguages")
        XCTAssertEqual(AppLanguage.stored(in: defaults, domain: domain), .system)
        defaults.set(["fr-CA", "en"], forKey: "AppleLanguages")
        XCTAssertEqual(AppLanguage.stored(in: defaults, domain: domain), .french)
    }

    func testTheLanguageNamesAreWrittenInTheirOwnLanguage() {
        XCTAssertEqual(AppLanguage.english.title, "English")
        XCTAssertEqual(AppLanguage.french.title, "Français")
        XCTAssertEqual(AppLanguage.allCases.map(\.rawValue), ["system", "en", "fr"])
    }

    func testTheLanguageFlagBecomesTheAppleLanguagesArgumentAndLeavesTheCommandAlone() {
        XCTAssertEqual(LanguageLaunch.plan(["Helix", "--render-snapshot", "a.kut", "b.png"]), .none)
        XCTAssertEqual(
            LanguageLaunch.plan(["Helix", "--render-inspector", "a.kut", "x", "b.png", "--language", "fr", "--dark"]),
            .restart(["Helix", "-AppleLanguages", "(fr)", "--render-inspector", "a.kut", "x", "b.png", "--dark"])
        )
        XCTAssertEqual(
            LanguageLaunch.plan(["Helix", "--language", "en", "--mcp"]),
            .restart(["Helix", "-AppleLanguages", "(en)", "--mcp"])
        )
        XCTAssertEqual(LanguageLaunch.plan(["Helix", "--language"]), .invalid)
        XCTAssertEqual(LanguageLaunch.plan(["Helix", "--language", "de"]), .invalid)
    }
}
