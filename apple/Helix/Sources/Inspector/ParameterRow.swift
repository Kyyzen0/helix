import SwiftUI

/// One parameter of the node: its label, its widget, and what is wrong with it.
struct ParameterRow: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel
    /// Whether the row sits in a group laid out side by side, where the group gives the label.
    var inline = false

    private var availability: RowAvailability { RowAvailability(parameter: parameter) }

    var body: some View {
        VStack(alignment: .leading, spacing: 3) {
            HStack(alignment: .firstTextBaseline, spacing: 8) {
                if !inline, !parameter.labelHidden { ParameterLabel(parameter: parameter) }
                if inline, !parameter.labelHidden, parameter.kind != .toggle {
                    Text(parameter.label).font(.caption).foregroundStyle(.secondary)
                }
                ParameterControl(parameter: parameter, model: model, availability: availability)
            }
            ParameterMessages(parameter: parameter, model: model)
        }
        .opacity(availability.isDisabled ? 0.4 : 1)
        .disabled(availability.isDisabled)
        .help(parameter.tooltip)
        .contextMenu { ParameterMenu(parameter: parameter, model: model, component: nil) }
    }
}

/// The label of a row, with the keyframe diamond when a component follows the timeline.
struct ParameterLabel: View {
    let parameter: InspectorParameter

    static let width: CGFloat = 104

    var body: some View {
        HStack(spacing: 3) {
            Text(parameter.label)
                .foregroundStyle(.secondary)
                .lineLimit(2)
                .fixedSize(horizontal: false, vertical: true)
            if !parameter.animated.isEmpty { KeyframeBadge(filled: !parameter.keyed.isEmpty) }
            Spacer(minLength: 0)
        }
        .frame(width: Self.width, alignment: .leading)
    }
}

/// What is wrong with the row, in red under it: an expression that does not run, or a value
/// the engine refused.
struct ParameterMessages: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel

    private var lines: [String] {
        var result: [String] = []
        for component in 0..<parameter.componentCount {
            if let error = parameter.expressionError(component) { result.append(ExpressionErrorText.short(error)) }
        }
        if let message = model.rowMessages[parameter.name], !result.contains(message) { result.append(message) }
        return result
    }

    var body: some View {
        ForEach(lines, id: \.self) { line in
            Text(line)
                .font(.caption)
                .foregroundStyle(.red)
                .fixedSize(horizontal: false, vertical: true)
                .padding(.leading, 0)
                .accessibilityLabel(Text("Error: \(line)"))
        }
    }
}

/// The context menu of a row or of one of its components.
struct ParameterMenu: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel
    /// The component the menu was asked on, nil for the row as a whole.
    let component: Int?

    var body: some View {
        let target = component ?? 0
        if parameter.takesExpression {
            Button("Edit Expression…") { model.beginExpression(parameter.name, component: target) }
                .disabled(parameter.isAnimated(target))
        }
        if let component, parameter.hasExpression(component) {
            Button("Clear Expression") { model.clearExpression(parameter.name, component: component) }
        } else if component == nil, parameter.hasAnyExpression {
            ForEach(Array(parameter.expressions.enumerated()).filter { !$0.element.isEmpty }, id: \.offset) { entry in
                Button(parameter.componentCount > 1 ? "Clear Expression \(Self.axisName(entry.offset))" : "Clear Expression") {
                    model.clearExpression(parameter.name, component: entry.offset)
                }
            }
        }
        if parameter.kind == .float {
            Divider()
            let scope = component.map { [$0] }
            let keyedHere = component.map { parameter.keyed.contains($0) } ?? !parameter.keyed.isEmpty
            let animatedHere = component.map { parameter.isAnimated($0) } ?? !parameter.animated.isEmpty
            Button("Set Keyframe") { model.setKeyframe(parameter.name, components: scope) }
            Button("Remove Keyframe") { model.removeKeyframe(parameter.name, components: scope) }
                .disabled(!keyedHere)
            Button("Clear Keyframes") { model.clearKeyframes(parameter.name, components: scope) }
                .disabled(!animatedHere)
        }
        Divider()
        Button("Reset to Default") { model.reset(parameter.name) }
            .disabled(parameter.isAtDefault || !parameter.animated.isEmpty)
    }

    static func axisName(_ component: Int) -> String {
        ["X", "Y", "Z", "W"][safe: component] ?? String(component + 1)
    }
}

/// The widget for the kind and style of a parameter.
struct ParameterControl: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel
    let availability: RowAvailability

    var body: some View {
        switch parameter.kind {
        case .float:
            if parameter.style == .rangeCircle, parameter.componentCount == 2 {
                RangeCircleControl(parameter: parameter, model: model, availability: availability)
            } else if parameter.componentCount > 1 {
                VectorControl(parameter: parameter, model: model, availability: availability)
            } else {
                ScalarControl(parameter: parameter, model: model, availability: availability)
            }
        case .int:
            if parameter.componentCount > 1 {
                VectorControl(parameter: parameter, model: model, availability: availability)
            } else {
                ScalarControl(parameter: parameter, model: model, availability: availability)
            }
        case .toggle:
            ToggleControl(parameter: parameter, model: model, availability: availability)
        case .dropdown:
            DropdownControl(parameter: parameter, model: model)
        case .string:
            StringControl(parameter: parameter, model: model, availability: availability)
        case .ramp:
            RampEditor(parameter: parameter, model: model)
        case .other:
            EmptyView()
        }
    }
}

// MARK: Numbers

/// Shows the expression, the draft of one, or the widget, for one component.
struct ComponentSlot<Widget: View>: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel
    let availability: RowAvailability
    let component: Int
    @ViewBuilder let widget: () -> Widget

    var body: some View {
        Group {
            if let draft = model.expressionDraft, draft.parameter == parameter.name, draft.component == component {
                ExpressionDraftField(
                    text: Binding(get: { model.expressionDraft?.text ?? "" }, set: { model.expressionDraft?.text = $0 }),
                    onCommit: { model.commitExpression() },
                    onCancel: { model.cancelExpression() }
                )
            } else if parameter.hasExpression(component) {
                ExpressionField(
                    text: parameter.expressions[component],
                    hasError: parameter.expressionError(component) != nil,
                    isEnabled: availability.canEdit(parameter, component: component)
                ) { model.beginExpression(parameter.name, component: component) }
            } else {
                widget()
                    .disabled(!availability.canEdit(parameter, component: component))
            }
        }
        .contextMenu {
            if parameter.componentCount > 1 { ParameterMenu(parameter: parameter, model: model, component: component) }
        }
    }
}

/// A float or an integer: a slider when there is a range to draw one over, and a field.
struct ScalarControl: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel
    let availability: RowAvailability

    private var isInteger: Bool { parameter.kind == .int }

    private var value: Double {
        isInteger ? Double(parameter.ints.first ?? 0) : (parameter.floats.first ?? 0)
    }

    var body: some View {
        ComponentSlot(parameter: parameter, model: model, availability: availability, component: 0) {
            HStack(spacing: 6) {
                if let range = parameter.range, range.hasSlider {
                    slider(in: range)
                }
                NumberField(
                    text: isInteger ? NumberText.format(parameter.ints.first ?? 0) : NumberText.format(value),
                    accessibilityName: parameter.label
                ) { typed in commit(typed) }
                .frame(width: 64)
                if isInteger { stepper }
            }
        }
    }

    private func slider(in range: InspectorRange) -> some View {
        Slider(
            value: Binding(
                get: { min(max(value, range.min), range.max) },
                set: { newValue in
                    if isInteger { model.setInt(parameter.name, component: 0, to: Int(newValue.rounded()), live: true) } else {
                        model.setFloat(parameter.name, component: 0, to: newValue, live: true)
                    }
                }
            ),
            in: range.min...range.max
        ) { editing in
            if editing { model.beginGesture(parameter.name) } else { model.endGesture() }
        }
        .controlSize(.small)
        .accessibilityLabel(Text(parameter.label))
    }

    private var stepper: some View {
        Stepper("", onIncrement: { step(1) }, onDecrement: { step(-1) })
            .labelsHidden()
    }

    private func step(_ direction: Int) {
        let current = parameter.ints.first ?? 0
        let next = parameter.range?.clamped(current + direction) ?? current + direction
        if next != current { model.setInt(parameter.name, component: 0, to: next) }
    }

    private func commit(_ typed: String) {
        if isInteger {
            switch NumberText.parseInt(typed) {
            case .value(let number): model.setInt(parameter.name, component: 0, to: number)
            case .invalid(let reason): model.report(reason, for: parameter.name)
            }
        } else {
            switch NumberText.parseFloat(typed) {
            case .value(let number): model.setFloat(parameter.name, component: 0, to: number)
            case .invalid(let reason): model.report(reason, for: parameter.name)
            }
        }
    }
}

/// A parameter of several numbers: X, Y and Z fields for the xyz style, plain fields otherwise.
/// Dragging over the axis letter scrubs the value.
struct VectorControl: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel
    let availability: RowAvailability

    private var isInteger: Bool { parameter.kind == .int }
    private let tints: [Color] = [.red, .green, .blue, .purple]

    var body: some View {
        HStack(spacing: 4) {
            ForEach(0..<parameter.componentCount, id: \.self) { component in
                ComponentSlot(parameter: parameter, model: model, availability: availability, component: component) {
                    HStack(spacing: 2) {
                        if parameter.style == .xyz || parameter.componentCount <= 3 {
                            scrub(component)
                        }
                        NumberField(
                            text: text(component),
                            accessibilityName: parameter.label + " " + ParameterMenu.axisName(component)
                        ) { typed in commit(typed, component: component) }
                    }
                }
            }
        }
    }

    private func current(_ component: Int) -> Double {
        isInteger ? Double(parameter.ints[safe: component] ?? 0) : (parameter.floats[safe: component] ?? 0)
    }

    private func text(_ component: Int) -> String {
        isInteger ? NumberText.format(parameter.ints[safe: component] ?? 0) : NumberText.format(current(component))
    }

    private func scrub(_ component: Int) -> some View {
        ScrubLabel(
            title: ParameterMenu.axisName(component),
            tint: parameter.style == .xyz ? tints[component % tints.count] : .secondary,
            isEnabled: availability.canEdit(parameter, component: component),
            current: { current(component) },
            range: parameter.range,
            integer: isInteger,
            begin: { model.beginGesture(parameter.name) },
            change: { newValue in
                if isInteger { model.setInt(parameter.name, component: component, to: Int(newValue), live: true) } else {
                    model.setFloat(parameter.name, component: component, to: newValue, live: true)
                }
            },
            end: { model.endGesture() }
        )
    }

    private func commit(_ typed: String, component: Int) {
        if isInteger {
            switch NumberText.parseInt(typed) {
            case .value(let number): model.setInt(parameter.name, component: component, to: number)
            case .invalid(let reason): model.report(reason, for: parameter.name)
            }
        } else {
            switch NumberText.parseFloat(typed) {
            case .value(let number): model.setFloat(parameter.name, component: component, to: number)
            case .invalid(let reason): model.report(reason, for: parameter.name)
            }
        }
    }
}

/// Two angles, the start and the end of an arc, drawn as a small dial and typed as two numbers.
struct RangeCircleControl: View {
    let parameter: InspectorParameter
    @ObservedObject var model: InspectorModel
    let availability: RowAvailability

    private var start: Double { parameter.floats[safe: 0] ?? 0 }
    private var end: Double { parameter.floats[safe: 1] ?? 360 }

    var body: some View {
        HStack(spacing: 8) {
            ArcDial(start: start, end: end)
                .frame(width: 30, height: 30)
                .accessibilityLabel(Text("\(parameter.label): \(NumberText.format(start)) to \(NumberText.format(end)) degrees"))
            VectorControl(parameter: parameter, model: model, availability: availability)
            if let caption = parameter.styleOptions["caption"], !caption.isEmpty {
                Text(caption).font(.caption).foregroundStyle(.secondary)
            }
        }
    }
}

/// An arc on a circle, from one angle to another, measured in degrees from the top, clockwise.
struct ArcDial: View {
    @Environment(\.theme) private var theme
    let start: Double
    let end: Double

    var body: some View {
        Canvas { context, size in
            let rect = CGRect(origin: .zero, size: size).insetBy(dx: 2, dy: 2)
            let centre = CGPoint(x: rect.midX, y: rect.midY)
            context.stroke(Path(ellipseIn: rect), with: .color(.secondary.opacity(0.45)), lineWidth: 1)
            var arc = Path()
            // Angles are from the top and run clockwise; Path measures from the right.
            arc.addArc(
                center: centre, radius: rect.width / 2,
                startAngle: .degrees(start - 90), endAngle: .degrees(end - 90), clockwise: false
            )
            context.stroke(arc, with: .color(theme.accent), style: StrokeStyle(lineWidth: 3, lineCap: .round))
        }
    }
}
