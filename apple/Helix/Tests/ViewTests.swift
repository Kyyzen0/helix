import XCTest

/// The rules behind the viewport views: the standard views and the cube drawn from the camera, the
/// grid label, the background colours, the layouts and the options of the headless commands. They
/// need no engine and no window.

final class StandardViewTests: XCTestCase {
    func testEachViewLooksFromTheSideItIsNamedFor() {
        for view in StandardView.allCases {
            let basis = CameraBasis(azimuth: Double(view.azimuth), elevation: Double(view.elevation))
            // The camera sits on the side of the model the view names; the top and the bottom are one
            // degree short of the pole, which the engine holds them at.
            XCTAssertGreaterThan(CameraBasis.dot(basis.toViewer, view.direction), 0.999, view.rawValue)
        }
    }

    func testTheFrontIsTheSideTheModelFacesAndTheScreenReadsAsTheWorld() {
        let basis = CameraBasis(azimuth: StandardView.front.azimuth.asDouble, elevation: 0)
        XCTAssertEqual(basis.right.x, 1, accuracy: 1e-9)
        XCTAssertEqual(basis.up.y, 1, accuracy: 1e-9)
        XCTAssertEqual(basis.toViewer.z, 1, accuracy: 1e-9)
        // The right view has the front of the model to its left.
        let right = CameraBasis(azimuth: 90, elevation: 0)
        XCTAssertEqual(right.right.z, -1, accuracy: 1e-9)
    }

    func testTheTopKeepsTheFrontTowardTheBottomOfTheScreen() {
        let basis = CameraBasis(azimuth: 0, elevation: 89)
        let front = basis.project(StandardView.front.direction)
        XCTAssertGreaterThan(front.y, 0.5, "y grows down the screen")
    }

    func testTheArgumentsOfTheCommandLineNameTheViews() {
        XCTAssertEqual(CameraView(argument: "home"), .home)
        XCTAssertEqual(CameraView(argument: "top"), .standard(.top))
        XCTAssertNil(CameraView(argument: "above"))
        XCTAssertEqual(StandardView.allCases.count, 6)
    }

    func testTheCommandKeysAreFree() {
        // ⌘1, ⌘2 and ⌘3 for the three views a person reaches for, none for the others.
        XCTAssertEqual(StandardView.allCases.compactMap(\.shortcutDigit), ["1", "2", "3"])
    }
}

final class ViewCubeTests: XCTestCase {
    private let size = 84.0

    private func cube(_ azimuth: Double, _ elevation: Double) -> ViewCubeGeometry {
        ViewCubeGeometry(basis: CameraBasis(azimuth: azimuth, elevation: elevation), size: size)
    }

    func testTheCubeStaysInsideItsSquare() {
        for azimuth in stride(from: -180.0, through: 180, by: 30) {
            for elevation in stride(from: -89.0, through: 89, by: 22) {
                for face in cube(azimuth, elevation).faces {
                    for corner in face.corners {
                        XCTAssertGreaterThanOrEqual(corner.x, 0)
                        XCTAssertLessThanOrEqual(corner.x, size)
                        XCTAssertGreaterThanOrEqual(corner.y, 0)
                        XCTAssertLessThanOrEqual(corner.y, size)
                    }
                }
            }
        }
    }

    func testSeenFromTheFrontOnlyTheFrontFaceShowsAndAClickOnItChoosesTheFront() {
        let front = cube(0, 0)
        XCTAssertEqual(front.visibleFaces.map(\.view), [.front])
        XCTAssertEqual(front.face(at: CGPoint(x: size / 2, y: size / 2)), .front)
        XCTAssertNil(front.face(at: CGPoint(x: 0, y: 0)), "off the cube")
    }

    func testFromTheHomeAngleThreeFacesShowAndEachOneIsUnderItsCentre() {
        // Home looks from the front-right, so the cube shows the faces a person expects to see first:
        // Front, Right and Top. It once sat at -135 and showed Back and Left.
        let state = ViewportCameraState()
        XCTAssertEqual(state.azimuth, 45)
        XCTAssertEqual(state.elevation, 25)
        let home = cube(Double(state.azimuth), Double(state.elevation))
        XCTAssertEqual(Set(home.visibleFaces.map(\.view)), [.front, .right, .top])
        for face in home.visibleFaces {
            XCTAssertEqual(home.face(at: face.center), face.view)
        }
    }

    func testFromAboveTheTopFaceIsHeadOn() {
        let top = cube(0, 89)
        XCTAssertEqual(top.face(at: CGPoint(x: size / 2, y: size / 2)), .top)
    }

    func testTheCubeTurnsWithTheCamera() {
        // Orbiting 90 degrees brings the right face where the front was.
        XCTAssertEqual(cube(90, 0).visibleFaces.map(\.view), [.right])
        XCTAssertEqual(cube(180, 0).visibleFaces.map(\.view), [.back])
        XCTAssertEqual(cube(0, -89).face(at: CGPoint(x: size / 2, y: size / 2)), .bottom)
    }
}

final class GridCellFormatTests: XCTestCase {
    private let english = Locale(identifier: "en_US")

    func testTheCellIsSaidInTheUnitThatKeepsTheNumberShort() {
        XCTAssertEqual(GridCellFormat.text(1, locale: english), "1 m")
        XCTAssertEqual(GridCellFormat.text(2, locale: english), "2 m")
        XCTAssertEqual(GridCellFormat.text(10, locale: english), "10 m")
        XCTAssertEqual(GridCellFormat.text(0.5, locale: english), "50 cm")
        XCTAssertEqual(GridCellFormat.text(0.05, locale: english), "5 cm")
        XCTAssertEqual(GridCellFormat.text(0.005, locale: english), "5 mm")
        XCTAssertEqual(GridCellFormat.text(1000, locale: english), "1 km")
    }

    func testTheNumberFollowsTheLanguage() {
        XCTAssertEqual(GridCellFormat.text(0.2, locale: Locale(identifier: "fr_FR")), "20 cm")
        XCTAssertEqual(GridCellFormat.text(1.5, locale: Locale(identifier: "fr_FR")), "1,5 m")
    }
}

final class ViewportBackgroundTests: XCTestCase {
    private let palettes: [ThemePalette] = [.light, .dark, .helix, .ember]

    func testTheGridCanBeToldFromTheBackgroundOnEveryBackgroundAndTheme() {
        for background in ViewportBackground.allCases {
            for palette in palettes {
                let colors = background.colors(for: palette)
                XCTAssertGreaterThanOrEqual(colors.grid.contrast(with: colors.background), 2, "\(background) \(palette.theme)")
            }
        }
    }

    func testTheGeometryStandsOutFromEveryBackground() {
        for background in ViewportBackground.allCases {
            for palette in palettes {
                let colors = background.colors(for: palette)
                XCTAssertGreaterThanOrEqual(colors.geometry.contrast(with: colors.background), 3, "\(background) \(palette.theme)")
            }
        }
    }

    func testTheThemeBackgroundIsTheOneOfThePalette() {
        for palette in palettes {
            XCTAssertEqual(ViewportBackground.theme.colors(for: palette).background, palette.viewportBackground)
        }
    }

    func testTheOverlaysFollowTheLuminanceOfTheBackground() {
        XCTAssertTrue(ViewportBackground.dark.colors(for: .light).isDark)
        XCTAssertTrue(ViewportBackground.medium.colors(for: .light).isDark)
        XCTAssertFalse(ViewportBackground.light.colors(for: .dark).isDark, "a light background gets dark text, whatever the theme")
        for palette in palettes {
            XCTAssertTrue(ViewportBackground.theme.colors(for: palette).isDark, "every theme paints a dark viewport today")
        }
    }

    func testTheStoredChoiceFallsBackToTheTheme() {
        let defaults = UserDefaults(suiteName: "ViewportBackgroundTests")!
        defaults.removePersistentDomain(forName: "ViewportBackgroundTests")
        XCTAssertEqual(ViewportBackground.stored(in: defaults), .theme)
        defaults.set("light", forKey: ViewportBackground.defaultsKey)
        XCTAssertEqual(ViewportBackground.stored(in: defaults), .light)
        defaults.set("nonsense", forKey: ViewportBackground.defaultsKey)
        XCTAssertEqual(ViewportBackground.stored(in: defaults), .theme)
    }
}

final class WindowLayoutTests: XCTestCase {
    private func freshDefaults() -> UserDefaults {
        let defaults = UserDefaults(suiteName: "WindowLayoutTests")!
        defaults.removePersistentDomain(forName: "WindowLayoutTests")
        return defaults
    }

    func testViewingGivesTheViewportTheRoomAndHidesTheInspector() {
        let defaults = freshDefaults()
        WindowLayout.viewing.apply(to: defaults)
        XCTAssertEqual(defaults.double(forKey: WindowLayout.fractionKey), 0.8)
        XCTAssertFalse(defaults.bool(forKey: InspectorSetting.defaultsKey))
        // It leaves the timeline and the bottom pane as the person had them.
        XCTAssertNil(defaults.object(forKey: TimelineSetting.modeKey))
        XCTAssertNil(defaults.object(forKey: TimelineSetting.lowerPaneKey))
    }

    func testBuildingIsTheLayoutTheWindowHadAlways() {
        let defaults = freshDefaults()
        WindowLayout.viewing.apply(to: defaults)
        WindowLayout.building.apply(to: defaults)
        XCTAssertEqual(defaults.double(forKey: WindowLayout.fractionKey), 0.6)
        XCTAssertTrue(defaults.bool(forKey: InspectorSetting.defaultsKey))
        XCTAssertEqual(defaults.string(forKey: TimelineSetting.lowerPaneKey), "network")
    }

    func testAnimatingShowsTheTimelineAndTakesRoomFromTheNetwork() {
        let defaults = freshDefaults()
        defaults.set(TimelineMode.never.rawValue, forKey: TimelineSetting.modeKey)
        WindowLayout.animating.apply(to: defaults)
        XCTAssertEqual(TimelineSetting.mode(in: defaults), .always)
        XCTAssertGreaterThan(defaults.double(forKey: WindowLayout.fractionKey), 0.6)
        XCTAssertLessThan(defaults.double(forKey: WindowLayout.fractionKey), 0.8)
    }

    func testEveryLayoutWritesOnlyTheKeysTheWindowAlreadyKeeps() {
        let known: Set<String> = [
            WindowLayout.fractionKey, InspectorSetting.defaultsKey, TimelineSetting.modeKey, TimelineSetting.lowerPaneKey,
        ]
        for layout in WindowLayout.allCases {
            XCTAssertTrue(Set(layout.settings.keys).isSubset(of: known), layout.rawValue)
        }
    }
}

final class ViewportOptionsTests: XCTestCase {
    func testTheOptionsComeOutOfTheArgumentsWhereverTheyStand() {
        let parsed = ViewportOptions.extract(from: ["in.kut", "--view", "top", "out.png", "--background", "light", "800", "600"])
        XCTAssertEqual(parsed?.options.view, .standard(.top))
        XCTAssertEqual(parsed?.options.background, .light)
        XCTAssertEqual(parsed?.rest, ["in.kut", "out.png", "800", "600"])
    }

    func testWithNoOptionTheCameraIsHomeAndTheBackgroundIsLeftAlone() {
        let parsed = ViewportOptions.extract(from: ["in.kut", "out.png"])
        XCTAssertEqual(parsed?.options.view, .home)
        XCTAssertNil(parsed?.options.background)
    }

    func testABadValueOrAMissingOneIsRefused() {
        XCTAssertNil(ViewportOptions.extract(from: ["--view", "sideways"]))
        XCTAssertNil(ViewportOptions.extract(from: ["--view"]))
        XCTAssertNil(ViewportOptions.extract(from: ["--background", "neon"]))
    }
}

private extension Float {
    var asDouble: Double { Double(self) }
}
