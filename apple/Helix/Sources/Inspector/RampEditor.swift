import SwiftUI

/// A ramp: a curve over its points, and a list of the points to edit. Dragging inside the curve
/// is not offered yet: the points are edited as numbers.
struct RampEditor: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel

    static let maxPoints = 64
    static let minPoints = 2

    private var points: [InspectorRampPoint] { parameter.rampPoints }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            RampCurve(points: points)
                .frame(height: 60)
            ForEach(Array(points.enumerated()), id: \.offset) { index, point in
                HStack(spacing: 4) {
                    NumberField(text: NumberText.format(point.position), accessibilityName: "Position") { typed in
                        edit(index, typed) { $0.position = $1 }
                    }
                    NumberField(text: NumberText.format(point.value), accessibilityName: "Value") { typed in
                        edit(index, typed) { $0.value = $1 }
                    }
                    Picker("Interpolation", selection: Binding(
                        get: { point.interpolation },
                        set: { newValue in
                            var next = points
                            next[index].interpolation = newValue
                            model.setRamp(parameter.name, points: next)
                        }
                    )) {
                        ForEach(parameter.rampInterpolations, id: \.value) { Text($0.label).tag($0.value) }
                    }
                    .labelsHidden()
                    .frame(width: 84)
                    Button {
                        var next = points
                        next.remove(at: index)
                        model.setRamp(parameter.name, points: next)
                    } label: { Image(systemName: "minus.circle") }
                    .buttonStyle(.borderless)
                    .disabled(points.count <= Self.minPoints)
                    .help(Text("Remove this point"))
                }
            }
            Button {
                model.setRamp(parameter.name, points: RampPoints.adding(to: points))
            } label: { Label("Add Point", systemImage: "plus.circle") }
            .buttonStyle(.borderless)
            .disabled(points.count >= Self.maxPoints)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    private func edit(_ index: Int, _ typed: String, _ change: (inout InspectorRampPoint, Double) -> Void) {
        switch NumberText.parseFloat(typed) {
        case .value(let number):
            var next = points
            change(&next[index], number)
            model.setRamp(parameter.name, points: next)
        case .invalid(let reason):
            model.report(reason, for: parameter.name)
        }
    }
}

/// The curve of a ramp, drawn from its points. Linear and constant segments are drawn as the
/// engine reads them, and a b spline segment as a smooth curve through the same points, which
/// is a drawing aid and not the sampled curve itself.
struct RampCurve: View {
    let points: [InspectorRampPoint]

    var body: some View {
        Canvas { context, size in
            let frame = CGRect(origin: .zero, size: size).insetBy(dx: 4, dy: 4)
            context.fill(Path(roundedRect: CGRect(origin: .zero, size: size), cornerRadius: 5), with: .color(.primary.opacity(0.06)))
            guard let low = points.map(\.value).min(), let high = points.map(\.value).max() else { return }
            let span = max(high - low, 1e-6)
            let sorted = points.sorted { $0.position < $1.position }
            guard let first = sorted.first, let last = sorted.last else { return }
            let width = max(last.position - first.position, 1e-6)
            func place(_ point: InspectorRampPoint) -> CGPoint {
                CGPoint(
                    x: frame.minX + (point.position - first.position) / width * frame.width,
                    y: frame.maxY - (point.value - low) / span * frame.height
                )
            }
            var curve = Path()
            for (index, point) in sorted.enumerated() {
                let here = place(point)
                if index == 0 {
                    curve.move(to: here)
                    continue
                }
                let before = place(sorted[index - 1])
                switch sorted[index - 1].interpolation {
                case "constant":
                    curve.addLine(to: CGPoint(x: here.x, y: before.y))
                    curve.addLine(to: here)
                case "bspline":
                    let middle = (here.x + before.x) / 2
                    curve.addCurve(to: here, control1: CGPoint(x: middle, y: before.y), control2: CGPoint(x: middle, y: here.y))
                default:
                    curve.addLine(to: here)
                }
            }
            context.stroke(curve, with: .color(.accentColor), lineWidth: 2)
            for point in sorted {
                let centre = place(point)
                context.fill(Path(ellipseIn: CGRect(x: centre.x - 3, y: centre.y - 3, width: 6, height: 6)), with: .color(.accentColor))
            }
        }
        .accessibilityLabel(Text("Ramp curve"))
    }
}
