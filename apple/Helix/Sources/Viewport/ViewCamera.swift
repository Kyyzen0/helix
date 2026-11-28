import Foundation
import SwiftUI

/// The six standard views of a model, named for the side the camera looks from.
///
/// The camera of the engine takes an azimuth and an elevation (`orbitViewportTo`). Azimuth 0 puts the
/// camera on the +z side looking toward the origin, which is where the front of a model faces in the
/// glTF convention the imports use; 90 puts it on +x. The elevation is held at 89 degrees by the
/// engine, so the top and the bottom are seen from one degree short of the pole: the view does not
/// flip, and the front of the model stays toward the bottom of the screen.
enum StandardView: String, CaseIterable, Identifiable, Sendable {
    case front, back, right, left, top, bottom

    var id: String { rawValue }

    /// The angle around the vertical axis, in degrees.
    var azimuth: Float {
        switch self {
        case .front, .top, .bottom: 0
        case .back: 180
        case .right: 90
        case .left: -90
        }
    }

    /// The angle above the horizontal plane, in degrees.
    var elevation: Float {
        switch self {
        case .top: 89
        case .bottom: -89
        default: 0
        }
    }

    /// The direction from the model toward the camera.
    var direction: SIMD3<Double> {
        switch self {
        case .front: SIMD3(0, 0, 1)
        case .back: SIMD3(0, 0, -1)
        case .right: SIMD3(1, 0, 0)
        case .left: SIMD3(-1, 0, 0)
        case .top: SIMD3(0, 1, 0)
        case .bottom: SIMD3(0, -1, 0)
        }
    }

    /// The name of the view, in the menus, on the faces of the cube and for VoiceOver.
    var title: LocalizedStringKey {
        switch self {
        case .front: LocalizedStringKey("Front")
        case .back: LocalizedStringKey("Back")
        case .right: LocalizedStringKey("Right")
        case .left: LocalizedStringKey("Left")
        case .top: LocalizedStringKey("Top")
        case .bottom: LocalizedStringKey("Bottom")
        }
    }

    /// The name as plain text, for sizing the label of a face to its letters.
    var localizedName: String {
        switch self {
        case .front: String(localized: "Front")
        case .back: String(localized: "Back")
        case .right: String(localized: "Right")
        case .left: String(localized: "Left")
        case .top: String(localized: "Top")
        case .bottom: String(localized: "Bottom")
        }
    }

    /// The digit of the command key the menu gives the view, for the three a person reaches for.
    var shortcutDigit: Character? {
        switch self {
        case .front: "1"
        case .right: "2"
        case .top: "3"
        default: nil
        }
    }
}

/// Where the camera is sent: back to where a scene starts, or to a standard view.
enum CameraView: Equatable, Sendable {
    case home
    case standard(StandardView)

    /// The view a `--view` argument names: `home`, or one of the six.
    init?(argument: String) {
        if argument == "home" {
            self = .home
        } else if let view = StandardView(rawValue: argument) {
            self = .standard(view)
        } else {
            return nil
        }
    }

    static let argumentNames = (["home"] + StandardView.allCases.map(\.rawValue)).joined(separator: "|") // l10n-ok: command line usage
}

/// Where the viewport camera is, as the view cube and the grid label need it.
struct ViewportCameraState: Equatable, Sendable {
    /// Degrees around the vertical axis and above the horizontal plane (see `StandardView`).
    var azimuth: Float = 45
    var elevation: Float = 25
    var distance: Float = 15
    /// The side of a cell of the ground grid, in metres.
    var gridCellSize: Float = 2

    /// The axes of the camera in world space: to the right of the screen, up the screen, and toward
    /// the viewer. The same maths as the camera of the engine, so a cube drawn from it is turned as
    /// the model is.
    var basis: CameraBasis { CameraBasis(azimuth: Double(azimuth), elevation: Double(elevation)) }
}

/// The right, up and toward-the-viewer axes of a camera, in world space.
struct CameraBasis: Equatable, Sendable {
    let right: SIMD3<Double>
    let up: SIMD3<Double>
    let toViewer: SIMD3<Double>

    init(azimuth: Double, elevation: Double) {
        let yaw = azimuth * .pi / 180, pitch = elevation * .pi / 180
        toViewer = SIMD3(cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw))
        let forward = -toViewer
        let world = SIMD3<Double>(0, 1, 0)
        let across = Self.cross(forward, world)
        let length = max((across * across).sum().squareRoot(), 1e-9)
        right = across / length
        up = Self.cross(right, forward)
    }

    /// Where a point of the world lands on a screen that looks along this camera: x to the right and
    /// y down (as a view is), with no perspective. The depth is how far toward the viewer it is.
    func project(_ point: SIMD3<Double>) -> (x: Double, y: Double, depth: Double) {
        (Self.dot(point, right), -Self.dot(point, up), Self.dot(point, toViewer))
    }

    static func dot(_ first: SIMD3<Double>, _ second: SIMD3<Double>) -> Double { (first * second).sum() }

    static func cross(_ first: SIMD3<Double>, _ second: SIMD3<Double>) -> SIMD3<Double> {
        SIMD3(
            first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x
        )
    }
}

/// One face of the view cube, seen from the camera.
struct CubeFace: Equatable {
    let view: StandardView
    /// The corners on the screen, in points from the top left of the cube's square.
    let corners: [CGPoint]
    /// How squarely the face turns to the viewer, from -1 (its back) to 1 (head on).
    let facing: Double

    var isVisible: Bool { facing > 0.02 }

    var center: CGPoint {
        CGPoint(
            x: corners.map(\.x).reduce(0, +) / Double(corners.count),
            y: corners.map(\.y).reduce(0, +) / Double(corners.count)
        )
    }

    /// Whether a point of the cube's square is over the face.
    func contains(_ point: CGPoint) -> Bool {
        // The face is a convex quadrilateral: the point is inside when it is on the same side of
        // every edge.
        var sign = 0.0
        for index in corners.indices {
            let from = corners[index], to = corners[(index + 1) % corners.count]
            let side = (to.x - from.x) * (point.y - from.y) - (to.y - from.y) * (point.x - from.x)
            if abs(side) < 1e-9 { continue }
            if sign == 0 { sign = side } else if sign * side < 0 { return false }
        }
        return true
    }
}

/// The view cube drawn from a camera: its six faces on a square of a size, and what is under a click.
struct ViewCubeGeometry {
    let size: Double
    let faces: [CubeFace]

    init(basis: CameraBasis, size: Double) {
        self.size = size
        // The cube spans -1 to 1; the scale keeps its longest diagonal inside the square.
        let scale = size / 2 / 3.0.squareRoot() * 0.98
        var faces: [CubeFace] = []
        for view in StandardView.allCases {
            let normal = view.direction
            // The two axes the face lies along, which are the two the normal does not point down.
            let along = Self.tangents(of: normal)
            var corners: [CGPoint] = []
            for (a, b) in [(-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)] {
                let point = normal + along.0 * a + along.1 * b
                let projected = basis.project(point)
                corners.append(CGPoint(x: size / 2 + projected.x * scale, y: size / 2 + projected.y * scale))
            }
            faces.append(CubeFace(view: view, corners: corners, facing: CameraBasis.dot(normal, basis.toViewer)))
        }
        self.faces = faces
    }

    private static func tangents(of normal: SIMD3<Double>) -> (SIMD3<Double>, SIMD3<Double>) {
        if normal.x != 0 { return (SIMD3(0, 1, 0), SIMD3(0, 0, 1)) }
        if normal.y != 0 { return (SIMD3(1, 0, 0), SIMD3(0, 0, 1)) }
        return (SIMD3(1, 0, 0), SIMD3(0, 1, 0))
    }

    /// The faces facing the viewer, the farthest first, so drawing them in order is right.
    var visibleFaces: [CubeFace] {
        faces.filter(\.isVisible).sorted { $0.facing < $1.facing }
    }

    /// The face under a point, the one nearest the viewer when two overlap, or nil off the cube.
    func face(at point: CGPoint) -> StandardView? {
        faces.filter { $0.isVisible && $0.contains(point) }.max { $0.facing < $1.facing }?.view
    }
}

/// The size of a grid cell as the label of the viewport says it: "2 m", "50 cm".
enum GridCellFormat {
    static func text(_ metres: Double, locale: Locale = .current) -> String {
        let (value, unit): (Double, String) = switch metres {
        case 1000...: (metres / 1000, "km")
        case 1...: (metres, "m")
        case 0.01...: (metres * 100, "cm")
        default: (metres * 1000, "mm")
        }
        let number = value.formatted(.number.locale(locale).precision(.fractionLength(0...2)))
        return "\(number) \(unit)"
    }
}
