import XCTest

/// The mapping from gestures to camera moves, preset by preset. It needs no engine and no window:
/// `HelixMetalView` only describes an event and applies the answer.
final class NavigationMapperTests: XCTestCase {
    private let defaults = NavigationOptions()

    private func scroll(
        _ dx: Float, _ dy: Float, precise: Bool = true, momentum: Bool = false, _ modifiers: NavigationModifiers,
        _ preset: NavigationPreset, _ options: NavigationOptions? = nil
    ) -> NavigationMove? {
        NavigationMapper.scroll(
            NavigationScroll(deltaX: dx, deltaY: dy, precise: precise, momentum: momentum, modifiers: modifiers),
            preset: preset, options: options ?? defaults
        )
    }

    private func scroll(
        _ dx: Float, _ dy: Float, precise: Bool = true, momentum: Bool = false,
        _ preset: NavigationPreset, _ options: NavigationOptions? = nil
    ) -> NavigationMove? {
        scroll(dx, dy, precise: precise, momentum: momentum, [], preset, options)
    }

    private func drag(
        _ dx: Float, _ dy: Float, _ button: NavigationButton, _ modifiers: NavigationModifiers,
        _ preset: NavigationPreset, _ options: NavigationOptions? = nil
    ) -> NavigationMove? {
        NavigationMapper.drag(NavigationDrag(deltaX: dx, deltaY: dy, button: button, modifiers: modifiers), preset: preset, options: options ?? defaults)
    }

    private func drag(
        _ dx: Float, _ dy: Float, _ button: NavigationButton, _ preset: NavigationPreset, _ options: NavigationOptions? = nil
    ) -> NavigationMove? {
        drag(dx, dy, button, [], preset, options)
    }

    private func isOrbit(_ move: NavigationMove?) -> Bool { if case .orbit = move { true } else { false } }
    private func isPan(_ move: NavigationMove?) -> Bool { if case .pan = move { true } else { false } }
    private func isDolly(_ move: NavigationMove?) -> Bool { if case .dolly = move { true } else { false } }

    // MARK: Trackpad

    func testTrackpadScrollOrbitsAndShiftPansAndCommandZooms() {
        XCTAssertEqual(scroll(3, 4, .trackpad), .orbit(3, 4))
        XCTAssertEqual(scroll(3, 4, [.shift], .trackpad), .pan(3, 4))
        XCTAssertEqual(scroll(3, 4, [.command], .trackpad), .dolly(4 * NavigationSettings.scrollZoomPerPoint))
    }

    func testTrackpadWheelWithoutPreciseDeltasStillZooms() {
        XCTAssertEqual(scroll(0, 2, precise: false, .trackpad), .dolly(2 * NavigationSettings.wheelZoomPerLine))
    }

    func testTrackpadDrags() {
        XCTAssertEqual(drag(2, 3, .left, .trackpad), .orbit(2, 3))
        XCTAssertEqual(drag(2, 3, .left, [.option], .trackpad), .pan(2, 3))
        XCTAssertEqual(drag(2, 3, .left, [.control], .trackpad), .dolly(3 * NavigationSettings.dragZoomPerPoint))
        XCTAssertEqual(drag(2, 3, .right, .trackpad), .dolly(3 * NavigationSettings.dragZoomPerPoint))
        XCTAssertEqual(drag(2, 3, .middle, .trackpad), .pan(2, 3))
        // Shift does not pan a drag on the trackpad preset: it is the preset of today.
        XCTAssertEqual(drag(2, 3, .left, [.shift], .trackpad), .orbit(2, 3))
    }

    // MARK: Mouse

    func testMouseScrollZoomsWhetherPreciseOrNot() {
        // A Magic Mouse scrolls with precise deltas, and used to orbit.
        XCTAssertEqual(scroll(0, 5, precise: true, .mouse), .dolly(5 * NavigationSettings.scrollZoomPerPoint))
        XCTAssertEqual(scroll(0, 2, precise: false, .mouse), .dolly(2 * NavigationSettings.wheelZoomPerLine))
        // The horizontal part of a scroll does not zoom.
        XCTAssertNil(scroll(5, 0, .mouse))
    }

    func testMouseShiftScrollPansAndOptionScrollOrbits() {
        XCTAssertEqual(scroll(3, 4, [.shift], .mouse), .pan(3, 4))
        XCTAssertEqual(scroll(3, 4, [.option], .mouse), .orbit(3, 4))
        // A line of a notched wheel is worth ten points.
        XCTAssertEqual(scroll(0, 2, precise: false, [.shift], .mouse), .pan(0, 20))
        XCTAssertEqual(scroll(0, 2, precise: false, [.option], .mouse), .orbit(0, 20))
    }

    func testMouseDrags() {
        XCTAssertEqual(drag(2, 3, .left, .mouse), .orbit(2, 3))
        XCTAssertEqual(drag(2, 3, .left, [.option], .mouse), .pan(2, 3))
        XCTAssertEqual(drag(2, 3, .left, [.shift], .mouse), .pan(2, 3))
        XCTAssertEqual(drag(2, 3, .left, [.control], .mouse), .dolly(3 * NavigationSettings.dragZoomPerPoint))
        XCTAssertEqual(drag(2, 3, .right, .mouse), .dolly(3 * NavigationSettings.dragZoomPerPoint))
        XCTAssertEqual(drag(2, 3, .middle, .mouse), .pan(2, 3))
    }

    // MARK: Blender

    func testBlenderMiddleDragOrbitsPansAndZooms() {
        XCTAssertEqual(drag(2, 3, .middle, .blender), .orbit(2, 3))
        XCTAssertEqual(drag(2, 3, .middle, [.shift], .blender), .pan(2, 3))
        XCTAssertEqual(drag(2, 3, .middle, [.control], .blender), .dolly(3 * NavigationSettings.dragZoomPerPoint))
    }

    func testBlenderLeftDragOrbitsSoNoMiddleButtonIsNeeded() {
        XCTAssertEqual(drag(2, 3, .left, .blender), .orbit(2, 3))
        XCTAssertEqual(drag(2, 3, .left, [.option], .blender), .pan(2, 3))
        XCTAssertNil(drag(2, 3, .right, .blender), "the right button does nothing in this preset")
    }

    func testBlenderWheelZoomsAndTheModifiersMoveTheView() {
        XCTAssertEqual(scroll(0, 2, precise: false, .blender), .dolly(2 * NavigationSettings.wheelZoomPerLine))
        XCTAssertEqual(scroll(0, 2, precise: false, [.shift], .blender), .pan(0, 20))
        XCTAssertEqual(scroll(0, 2, precise: false, [.option], .blender), .orbit(0, 20))
    }

    // MARK: Pinch and rotation

    func testAPinchZoomsAndATwoFingerRotationOrbitsInEveryPreset() {
        XCTAssertEqual(NavigationMapper.pinch(magnification: 0.1, options: defaults), .dolly(0.1 * NavigationSettings.pinchZoom))
        XCTAssertEqual(NavigationMapper.rotation(degrees: 2, options: defaults), .orbit(2 * NavigationSettings.rotationPerDegree, 0))
        XCTAssertNil(NavigationMapper.pinch(magnification: 0, options: defaults))
    }

    // MARK: Options

    func testInvertingTheZoomReversesEveryZoomAndNothingElse() {
        var options = defaults
        options.invertZoom = true
        XCTAssertEqual(scroll(0, 2, precise: false, .mouse, options), .dolly(-2 * NavigationSettings.wheelZoomPerLine))
        XCTAssertEqual(scroll(0, 2, [.command], .trackpad, options), .dolly(-2 * NavigationSettings.scrollZoomPerPoint))
        XCTAssertEqual(drag(0, 3, .right, .mouse, options), .dolly(-3 * NavigationSettings.dragZoomPerPoint))
        XCTAssertEqual(NavigationMapper.pinch(magnification: 0.1, options: options), .dolly(-0.1 * NavigationSettings.pinchZoom))
        XCTAssertEqual(scroll(3, 4, .trackpad, options), .orbit(3, 4))
        XCTAssertEqual(scroll(3, 4, [.shift], .trackpad, options), .pan(3, 4))
    }

    func testTheSpeedsScaleTheirOwnMovesOnly() {
        var options = defaults
        options.orbitSpeed = 2
        options.zoomSpeed = 0.5
        XCTAssertEqual(scroll(3, 4, .trackpad, options), .orbit(6, 8))
        XCTAssertEqual(drag(3, 4, .left, .mouse, options), .orbit(6, 8))
        XCTAssertEqual(NavigationMapper.rotation(degrees: 2, options: options), .orbit(2 * NavigationSettings.rotationPerDegree * 2, 0))
        XCTAssertEqual(scroll(3, 4, [.shift], .trackpad, options), .pan(3, 4), "the pan is not scaled")
        XCTAssertEqual(scroll(0, 2, precise: false, .mouse, options), .dolly(2 * NavigationSettings.wheelZoomPerLine * 0.5))
        XCTAssertEqual(NavigationMapper.pinch(magnification: 0.1, options: options), .dolly(0.1 * NavigationSettings.pinchZoom * 0.5))
    }

    func testTheInertiaOfAScrollDoesNotOrbitButStillZoomsAndPans() {
        // On by default.
        XCTAssertNil(scroll(3, 4, momentum: true, .trackpad))
        XCTAssertNil(scroll(3, 4, momentum: true, [.option], .mouse))
        XCTAssertEqual(scroll(3, 4, momentum: true, [.shift], .trackpad), .pan(3, 4))
        XCTAssertEqual(scroll(0, 4, momentum: true, [.command], .trackpad), .dolly(4 * NavigationSettings.scrollZoomPerPoint))
        XCTAssertEqual(scroll(0, 4, momentum: true, .mouse), .dolly(4 * NavigationSettings.scrollZoomPerPoint))
        // Off: the inertia orbits like the fingers did.
        var options = defaults
        options.ignoreScrollInertia = false
        XCTAssertEqual(scroll(3, 4, momentum: true, .trackpad, options), .orbit(3, 4))
        // The fingers themselves always orbit.
        XCTAssertEqual(scroll(3, 4, momentum: false, .trackpad), .orbit(3, 4))
    }

    func testAMoveThatMovesNothingIsNoMove() {
        XCTAssertNil(scroll(0, 0, .trackpad))
        XCTAssertNil(drag(0, 0, .left, .trackpad))
    }

    // MARK: Stored choices

    func testTheStoredOptionsAreReadAndHandEditedValuesAreBroughtBack() throws {
        let defaults = try XCTUnwrap(UserDefaults(suiteName: "NavigationOptionsTests-\(UUID().uuidString)"))
        XCTAssertEqual(NavigationOptions.stored(in: defaults), NavigationOptions(), "nothing stored: today's behaviour")
        XCTAssertEqual(NavigationPreset.stored(in: defaults), .trackpad)
        defaults.set("mouse", forKey: NavigationPreset.defaultsKey)
        defaults.set(true, forKey: NavigationOptions.invertZoomKey)
        defaults.set(2.0, forKey: NavigationOptions.orbitSpeedKey)
        defaults.set(100.0, forKey: NavigationOptions.zoomSpeedKey)
        defaults.set(false, forKey: NavigationOptions.ignoreInertiaKey)
        XCTAssertEqual(NavigationPreset.stored(in: defaults), .mouse)
        let options = NavigationOptions.stored(in: defaults)
        XCTAssertTrue(options.invertZoom)
        XCTAssertEqual(options.orbitSpeed, 2)
        XCTAssertEqual(options.zoomSpeed, 4, "a speed past the slider is brought to its end")
        XCTAssertFalse(options.ignoreScrollInertia)
        defaults.set("gamepad", forKey: NavigationPreset.defaultsKey)
        XCTAssertEqual(NavigationPreset.stored(in: defaults), .trackpad, "an unknown preset is the default")
        XCTAssertEqual(NavigationOptions.clampedSpeed(.nan), 1)
        XCTAssertEqual(NavigationOptions.clampedSpeed(0), 0.25)
    }

    // MARK: The legend tells the truth

    func testEveryPresetHasALegendWithDistinctGesturesAndF() {
        for preset in NavigationPreset.allCases {
            let rows = NavigationBindings.rows(for: preset)
            XCTAssertEqual(Set(rows.map(\.gesture)).count, rows.count, preset.rawValue)
            XCTAssertTrue(rows.contains { $0.gesture == "F" }, preset.rawValue)
        }
    }

    func testTheMouseLegendSaysScrollZoomsAndTheMapperDoesIt() {
        let rows = NavigationBindings.rows(for: .mouse)
        XCTAssertEqual(rows.first { $0.gesture == "Scroll, or pinch" }?.action, "Zoom")
        XCTAssertEqual(rows.first { $0.gesture == "⇧ scroll" }?.action, "Pan")
        XCTAssertEqual(rows.first { $0.gesture == "⌥ scroll" }?.action, "Orbit")
        XCTAssertTrue(isDolly(scroll(0, 1, .mouse)))
        XCTAssertTrue(isPan(scroll(1, 1, [.shift], .mouse)))
        XCTAssertTrue(isOrbit(scroll(1, 1, [.option], .mouse)))
    }

    func testTheBlenderLegendNamesTheMiddleButton() {
        let gestures = NavigationBindings.rows(for: .blender).map(\.gesture)
        XCTAssertTrue(gestures.contains { $0.contains("Middle button") })
        XCTAssertTrue(gestures.contains { $0.contains("⌃ middle button") })
    }

    func testTheLegendsAreNotTheSameForTwoPresets() {
        XCTAssertNotEqual(NavigationBindings.rows(for: .trackpad), NavigationBindings.rows(for: .mouse))
        XCTAssertNotEqual(NavigationBindings.rows(for: .mouse), NavigationBindings.rows(for: .blender))
    }

    func testTheHeadlessCommandsTakeANavigationPreset() throws {
        let (options, rest) = try XCTUnwrap(ViewportOptions.extract(from: ["a.kut", "--navigation", "mouse", "out.png"]))
        XCTAssertEqual(options.navigation, .mouse)
        XCTAssertEqual(rest, ["a.kut", "out.png"])
        XCTAssertNil(ViewportOptions.extract(from: ["--navigation", "gamepad"]))
        XCTAssertNil(ViewportOptions.extract(from: ["--navigation"]))
    }
}
