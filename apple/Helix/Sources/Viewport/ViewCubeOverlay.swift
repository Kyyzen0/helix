import SwiftUI

/// The overlay at the top right of the viewport: an axis tripod, the view cube and a Home button.
///
/// The cube and the tripod are drawn from the camera of the viewport (the controller publishes it
/// after every frame), so they turn with the model as the person orbits. A click on a face goes to
/// that standard view and frames the geometry.
struct ViewCubeOverlay: View {
    @ObservedObject var controller: ViewportController
    @Environment(\.interfaceSize) private var size

    var body: some View {
        let basis = controller.camera.basis
        HStack(alignment: .center, spacing: size.points(6)) {
            AxisTripod(basis: basis)
                .frame(width: size.points(56), height: size.points(56))
            ViewCube(basis: basis) { controller.show($0) }
                .frame(width: size.points(100), height: size.points(100))
            Button { controller.goHome() } label: {
                Image(systemName: "house")
                    .font(.system(size: size.points(13), weight: .medium))
                    .frame(width: size.points(24), height: size.points(24))
                    .contentShape(Rectangle())
            }
            .buttonStyle(.borderless)
            .help("Back to the starting view")
            .accessibilityLabel(Text("Home view"))
        }
        .padding(size.points(6))
        .background(RoundedRectangle(cornerRadius: 10).fill(.regularMaterial))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color.primary.opacity(0.12)))
        .accessibilityElement(children: .contain)
        .accessibilityLabel(Text("View cube"))
    }
}

/// The cube: six faces drawn from the camera, the nearest ones labelled.
struct ViewCube: View {
    let basis: CameraBasis
    let onChoose: (StandardView) -> Void
    @State private var hovered: StandardView?
    @Environment(\.colorScheme) private var scheme
    @Environment(\.theme) private var theme

    var body: some View {
        GeometryReader { proxy in
            let side = Double(min(proxy.size.width, proxy.size.height))
            let cube = ViewCubeGeometry(basis: basis, size: side)
            Canvas { context, _ in
                for face in cube.visibleFaces {
                    var path = Path()
                    path.addLines(face.corners)
                    path.closeSubpath()
                    // A face turned away is darker: the cube reads as a solid.
                    let shade = 0.55 + 0.45 * face.facing
                    let isHovered = hovered == face.view
                    let base = scheme == .dark ? Color.white : Color.black
                    context.fill(path, with: .color(isHovered ? theme.accent.opacity(0.85) : base.opacity(0.10 + 0.14 * shade)))
                    context.stroke(path, with: .color(base.opacity(0.45)), lineWidth: 1)
                    // The label is sized to fit the face as it is seen, so two words on neighbouring faces
                    // do not run into each other, and a sliver carries none.
                    let xs = face.corners.map(\.x), ys = face.corners.map(\.y)
                    let words = Double(face.view.localizedName.count)
                    let fit = min(
                        ((xs.max() ?? 0) - (xs.min() ?? 0)) * 0.9 / (max(words, 3) * 0.6),
                        ((ys.max() ?? 0) - (ys.min() ?? 0)) * 0.6,
                        side * 0.12
                    )
                    if fit >= 6.5 {
                        let label = context.resolve(Text(face.view.title).font(.system(size: fit, weight: .semibold)))
                        context.draw(label, at: face.center)
                    }
                }
            }
            .contentShape(Rectangle())
            .onContinuousHover { phase in
                switch phase {
                case .active(let point): hovered = cube.face(at: point)
                case .ended: hovered = nil
                }
            }
            .gesture(
                SpatialTapGesture().onEnded { tap in
                    if let view = cube.face(at: tap.location) { onChoose(view) }
                }
            )
            // The faces as buttons for VoiceOver and the keyboard, which cannot click the drawing.
            .accessibilityRepresentation {
                HStack {
                    ForEach(StandardView.allCases) { view in
                        Button { onChoose(view) } label: { Text(view.title) }
                            .accessibilityLabel(Text(view.title))
                    }
                }
            }
        }
    }
}

/// The three axes of the world as the camera sees them: X red, Y green, Z blue, each with its letter.
struct AxisTripod: View {
    let basis: CameraBasis
    @Environment(\.colorScheme) private var scheme

    var body: some View {
        Canvas { context, canvasSize in
            let centre = CGPoint(x: canvasSize.width / 2, y: canvasSize.height / 2)
            let length = min(canvasSize.width, canvasSize.height) * 0.30
            let axes: [(String, SIMD3<Double>, Color)] = [
                ("X", SIMD3(1, 0, 0), scheme == .dark ? Color(red: 1.0, green: 0.42, blue: 0.40) : Color(red: 0.80, green: 0.14, blue: 0.14)),
                ("Y", SIMD3(0, 1, 0), scheme == .dark ? Color(red: 0.45, green: 0.88, blue: 0.40) : Color(red: 0.10, green: 0.55, blue: 0.14)),
                ("Z", SIMD3(0, 0, 1), scheme == .dark ? Color(red: 0.45, green: 0.65, blue: 1.0) : Color(red: 0.12, green: 0.30, blue: 0.85)),
            ]
            // The axes pointing away are drawn first, so the ones toward the viewer cover them.
            let placed = axes.map { axis in (axis, basis.project(axis.1)) }.sorted { $0.1.depth < $1.1.depth }
            for (axis, projected) in placed {
                let end = CGPoint(x: centre.x + projected.x * length, y: centre.y + projected.y * length)
                var line = Path()
                line.move(to: centre)
                line.addLine(to: end)
                context.stroke(line, with: .color(axis.2), style: StrokeStyle(lineWidth: 1.6, lineCap: .round))
                // The letter sits just past the tip, pushed out along the axis.
                let reach = max((projected.x * projected.x + projected.y * projected.y).squareRoot(), 0.001)
                let letter = CGPoint(
                    x: end.x + projected.x / reach * 6,
                    y: end.y + projected.y / reach * 6
                )
                context.draw(
                    Text(verbatim: axis.0).font(.system(size: 9, weight: .bold)).foregroundColor(axis.2),
                    at: letter
                )
            }
        }
        .accessibilityHidden(true)
    }
}

/// The size of a cell of the grid, quietly, in a corner of the viewport.
struct GridLabel: View {
    /// Observed, not read once: the cell size changes as the person zooms, and only the
    /// controller says so; the view around the label is not redrawn then.
    @ObservedObject var controller: ViewportController
    @Environment(\.interfaceSize) private var size

    private var cell: Float { controller.camera.gridCellSize }

    var body: some View {
        Text("Grid: \(GridCellFormat.text(Double(cell)))")
            .uiFont(.caption)
            .monospacedDigit()
            .foregroundStyle(.secondary)
            .padding(.horizontal, size.points(8))
            .padding(.vertical, size.points(3))
            .background(Capsule().fill(.regularMaterial))
            .allowsHitTesting(false)
            .accessibilityLabel(Text("Grid: \(GridCellFormat.text(Double(cell)))"))
    }
}
