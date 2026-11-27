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
        case .auto: "Wireframe: Auto"
        case .on: "Wireframe: On"
        case .off: "Wireframe: Off"
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

/// What the document and the menu use to reach the viewport of a window.
///
/// The camera itself lives in the facade. The controller only holds the view, so that a request
/// for a new frame has somewhere to go, and the one request that has to wait for the size of the
/// view to be known.
@MainActor
final class ViewportController {
    weak var view: MTKView?

    nonisolated init() {}

    /// Set when the camera should frame the geometry on the next frame drawn, since the aspect
    /// ratio it needs is only known there.
    private(set) var framePending = false

    /// Called when the renderer cannot draw, with a message for a person.
    var onFailure: ((String) -> Void)?

    func requestFrame() {
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
}
