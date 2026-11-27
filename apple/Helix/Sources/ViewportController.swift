import HelixCore
import MetalKit
import SwiftUI

/// Whether the viewport draws the edges of the mesh over its shading: the View menu setting.
///
/// It is kept in the user defaults, so every window shares it and it survives a restart.
enum WireframeSetting: String, CaseIterable, Identifiable {
    /// Drawn while the mesh is light enough to read through it, and faded out as it gets dense.
    case auto
    case on
    case off

    static let defaultsKey = "wireframeMode"

    var id: String { rawValue }

    /// The setting the user chose, and `auto` before they chose any.
    static var current: WireframeSetting {
        UserDefaults.standard.string(forKey: defaultsKey).flatMap(WireframeSetting.init(rawValue:)) ?? .auto
    }

    var title: LocalizedStringKey {
        switch self {
        case .auto: LocalizedStringKey("Wireframe: Auto")
        case .on: LocalizedStringKey("Wireframe: On")
        case .off: LocalizedStringKey("Wireframe: Off")
        }
    }

    /// The setting the shortcut moves to: auto, on, off, and around.
    var next: WireframeSetting {
        switch self {
        case .auto: .on
        case .on: .off
        case .off: .auto
        }
    }

    var facadeMode: helix.core.WireframeMode {
        switch self {
        case .auto: .Auto
        case .on: .On
        case .off: .Off
        }
    }
}

/// Whether the viewport draws the base colour and the picture of the materials: the View menu setting.
///
/// Kept in the user defaults like the wireframe, so every window shares it. Materials are shown
/// until the user hides them.
enum MaterialsSetting {
    static let defaultsKey = "materialsVisible"

    /// The setting the user chose, and `true` before they chose any.
    static var isVisible: Bool {
        let defaults = UserDefaults.standard
        // bool(forKey:) also reads the YES and NO of a launch argument, which arrives as text.
        return defaults.object(forKey: defaultsKey) == nil ? true : defaults.bool(forKey: defaultsKey)
    }
}

/// What the document and the menu use to reach the viewport of a window.
///
/// The camera itself lives in the facade. The controller only holds the view, so that a request
/// for a new frame has somewhere to go, and the one request that has to wait for the size of the
/// view to be known.
@MainActor
final class ViewportController {
    weak var view: MTKView?

    /// The timeline of the window, which the space bar and the arrow keys drive while the viewport
    /// has the keys.
    weak var timeline: TimelineModel?

    nonisolated init() {}

    /// Set when the camera should frame the geometry on the next frame drawn, since the aspect
    /// ratio it needs is only known there.
    private(set) var framePending = false

    /// Called when the renderer cannot draw, with a message for a person.
    var onFailure: ((String) -> Void)?

    /// How many times the camera was asked to frame the geometry. `Scripts/launch-smoke.py` reads it
    /// (`HELIX_DEBUG_FRAMING_CHECK`) to see that an edit leaves the camera alone.
    private(set) var frameRequestCount = 0

    func requestFrame() {
        frameRequestCount += 1
        framePending = true
        redraw()
    }

    func takePendingFrame() -> Bool {
        defer { framePending = false }
        return framePending
    }

    func redraw() {
        view?.needsDisplay = true
    }

    /// Moves the wireframe setting to the next one, for the key that does it. The menu reads the
    /// same user default, so it follows.
    func cycleWireframe() {
        UserDefaults.standard.set(WireframeSetting.current.next.rawValue, forKey: WireframeSetting.defaultsKey)
        redraw()
    }

    func toggleMaterials() {
        UserDefaults.standard.set(!MaterialsSetting.isVisible, forKey: MaterialsSetting.defaultsKey)
        redraw()
    }
}
