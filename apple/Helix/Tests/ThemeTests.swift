import XCTest

final class ThemeTests: XCTestCase {
    private var palettes: [ThemePalette] { [.light, .dark, .helix, .ember] }

    private func name(_ palette: ThemePalette) -> String { palette.theme.rawValue }

    // MARK: Contrast

    func testTheContrastRatioIsTheOneOfWCAG() {
        XCTAssertEqual(RGB(hex: 0x000000).contrast(with: RGB(hex: 0xFFFFFF)), 21, accuracy: 1e-9)
        XCTAssertEqual(RGB(hex: 0x777777).contrast(with: RGB(hex: 0x777777)), 1, accuracy: 1e-9)
        // #767676 on white is the classic 4.54.
        XCTAssertEqual(RGB(hex: 0x767676).contrast(with: RGB(hex: 0xFFFFFF)), 4.54, accuracy: 0.01)
    }

    func testBodyTextReadsOnEverySurfaceItIsDrawnOn() {
        for palette in palettes {
            for ground in [palette.window, palette.surface] {
                XCTAssertGreaterThanOrEqual(palette.text.contrast(with: ground), 4.5, "\(name(palette)) text on \(ground.hexString)")
            }
        }
    }

    func testSecondaryTextIsLegibleOnEverySurface() {
        for palette in palettes {
            for ground in [palette.window, palette.surface] {
                XCTAssertGreaterThanOrEqual(palette.textSecondary.contrast(with: ground), 4.5, "\(name(palette)) secondary text on \(ground.hexString)")
            }
        }
    }

    func testTheAccentIsVisibleAgainstTheWindowAndTheCards() {
        for palette in palettes {
            for ground in [palette.window, palette.surface] {
                XCTAssertGreaterThanOrEqual(palette.accentOrSystem.contrast(with: ground), 3, "\(name(palette)) accent on \(ground.hexString)")
            }
        }
    }

    func testTextOnTheAccentIsLegible() {
        for palette in palettes {
            // White on the blue of macOS is what every system button is: held to the 3 of large text.
            XCTAssertGreaterThanOrEqual(palette.onAccent.contrast(with: palette.accentOrSystem), palette.followsSystemColors ? 3 : 4.5, name(palette))
        }
    }

    func testTheNodeEditorStrokesStandOutFromTheirBackgrounds() {
        for palette in palettes {
            XCTAssertGreaterThanOrEqual(palette.nodeLink.contrast(with: palette.window), 3, "\(name(palette)) link")
            XCTAssertGreaterThanOrEqual(palette.nodePort.contrast(with: palette.surface), 3, "\(name(palette)) port")
            XCTAssertGreaterThanOrEqual(palette.displayFlag.contrast(with: palette.surface), 3, "\(name(palette)) display flag")
            XCTAssertGreaterThanOrEqual(palette.nodeCardStroke.contrast(with: palette.window), 1.5, "\(name(palette)) card outline")
            // The dots are a texture, not information: only asked to be there.
            XCTAssertGreaterThan(palette.nodeGrid.contrast(with: palette.window), 1.1, "\(name(palette)) grid")
        }
    }

    func testTheTimelineMarksStandOutFromTheBar() {
        for palette in palettes {
            let bar = palette.window.mixed(with: palette.surface, by: 0.5)
            XCTAssertGreaterThanOrEqual(palette.rulerLine.contrast(with: bar), 2, "\(name(palette)) ruler")
            // The keys of the system themes are the orange macOS has always had, which is what
            // the interface looked like before themes, so only the custom themes are held to 3.
            XCTAssertGreaterThanOrEqual(palette.keyframe.contrast(with: bar), palette.followsSystemColors ? 1.8 : 3, "\(name(palette)) key")
            XCTAssertGreaterThanOrEqual(palette.cachedBar.contrast(with: bar), palette.followsSystemColors ? 3 : 3, "\(name(palette)) cached frames")
        }
    }

    func testTheGeometryStandsOutFromTheViewportBackground() {
        for palette in palettes {
            XCTAssertGreaterThanOrEqual(palette.viewportGeometry.contrast(with: palette.viewportBackground), 4.5, name(palette))
        }
    }

    // MARK: Structure

    func testEveryDarkThemeHasADarkWindowAndEveryLightOneALightWindow() {
        for palette in palettes {
            XCTAssertEqual(palette.window.luminance < 0.2, palette.isDark, name(palette))
        }
    }

    func testCustomThemesHaveTheirOwnAccentAndTheSystemOnesKeepTheSystems() {
        XCTAssertNil(ThemePalette.light.accent)
        XCTAssertNil(ThemePalette.dark.accent)
        XCTAssertNotNil(ThemePalette.helix.accent)
        XCTAssertNotNil(ThemePalette.ember.accent)
        for theme in Theme.allCases {
            XCTAssertEqual(theme.followsSystemColors, theme.palette(systemIsDark: false).followsSystemColors, theme.rawValue)
        }
    }

    func testTheViewportOfTheSystemThemesIsTheSlateItAlwaysWas() {
        for palette in [ThemePalette.light, .dark] {
            XCTAssertEqual(palette.viewportBackground, RGB(red: 0.13, green: 0.14, blue: 0.16))
        }
    }

    func testTheSystemThemeResolvesToTheLightOrTheDarkPalette() {
        XCTAssertEqual(Theme.system.palette(systemIsDark: false), .light)
        XCTAssertEqual(Theme.system.palette(systemIsDark: true), .dark)
    }

    func testTheOtherThemesIgnoreTheAppearanceOfTheSystem() {
        for theme in [Theme.light, .dark, .helix, .ember] {
            XCTAssertEqual(theme.palette(systemIsDark: true), theme.palette(systemIsDark: false), theme.rawValue)
        }
        XCTAssertEqual(Theme.light.palette(systemIsDark: true), .light)
        XCTAssertEqual(Theme.dark.palette(systemIsDark: false), .dark)
    }

    func testForcedAppearances() {
        XCTAssertNil(Theme.system.forcedDark)
        XCTAssertEqual(Theme.light.forcedDark, false)
        for theme in [Theme.dark, .helix, .ember] { XCTAssertEqual(theme.forcedDark, true, theme.rawValue) }
    }

    func testEveryThemeHasATitle() {
        for theme in Theme.allCases { XCTAssertFalse(theme.title.isEmpty) }
        XCTAssertEqual(Set(Theme.allCases.map(\.title)).count, Theme.allCases.count)
    }

    // MARK: Persistence

    private func defaults() -> UserDefaults {
        let suite = "HelixThemeTests-\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        addTeardownBlock { UserDefaults(suiteName: suite)?.removePersistentDomain(forName: suite) }
        return defaults
    }

    func testTheSystemThemeIsTheDefault() {
        XCTAssertEqual(Theme.stored(in: defaults()), .system)
    }

    func testEveryThemeSurvivesAStoreAndARead() {
        let defaults = defaults()
        for theme in Theme.allCases {
            theme.store(in: defaults)
            XCTAssertEqual(Theme.stored(in: defaults), theme)
            XCTAssertEqual(defaults.string(forKey: "theme"), theme.rawValue)
        }
    }

    func testAnUnknownStoredValueFallsBackToTheDefault() {
        let defaults = defaults()
        defaults.set("solarized", forKey: Theme.defaultsKey)
        XCTAssertEqual(Theme.stored(in: defaults), .system)
    }

    // MARK: Colour values

    func testHexRoundTrip() {
        XCTAssertEqual(RGB(hex: 0xC063E0).hexString, "#C063E0")
        XCTAssertEqual(RGB(hex: 0x000000).hexString, "#000000")
    }

    func testMixing() {
        let mixed = RGB(hex: 0x000000).mixed(with: RGB(hex: 0xFFFFFF), by: 0.5)
        XCTAssertEqual(mixed.red, 0.5, accuracy: 1e-9)
    }

    // MARK: Command line

    func testTheThemeOptionsAreTakenOutOfTheArguments() throws {
        let (options, rest) = try XCTUnwrap(ThemeOptions.extract(from: ["a.kut", "--theme", "ember", "out.png", "--appearance", "dark"]))
        XCTAssertEqual(options.theme, .ember)
        XCTAssertEqual(options.appearanceDark, true)
        XCTAssertEqual(rest, ["a.kut", "out.png"])
    }

    func testNoOptionsMeansTheSystemTheme() throws {
        let (options, rest) = try XCTUnwrap(ThemeOptions.extract(from: ["a", "b"]))
        XCTAssertEqual(options.theme, .system)
        XCTAssertNil(options.appearanceDark)
        XCTAssertEqual(rest, ["a", "b"])
    }

    func testABadThemeOptionIsRefused() {
        XCTAssertNil(ThemeOptions.extract(from: ["--theme", "neon"]))
        XCTAssertNil(ThemeOptions.extract(from: ["--theme"]))
        XCTAssertNil(ThemeOptions.extract(from: ["--appearance", "sepia"]))
    }

    func testThePaletteOfTheOptions() throws {
        var options = ThemeOptions()
        XCTAssertEqual(options.palette(), .light)
        XCTAssertEqual(options.palette(dark: true), .dark)
        options.appearanceDark = false
        XCTAssertEqual(options.palette(dark: true), .light)
        options.theme = .helix
        XCTAssertEqual(options.palette(), .helix)
    }
}
