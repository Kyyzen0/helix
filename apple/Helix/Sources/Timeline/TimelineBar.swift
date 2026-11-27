import SwiftUI

/// The bar at the bottom of a window: the controls of playback, the frame, the range and the rate,
/// and the ruler with the playhead and the keys of the node in front.
struct TimelineBar: View {
    @ObservedObject var model: TimelineModel
    @AppStorage(TimelineSetting.loopKey) private var loop = true

    var body: some View {
        VStack(spacing: 0) {
            Divider()
            controls
                .padding(.horizontal, 10)
                .padding(.top, 5)
            TimelineRulerView(model: model)
                .frame(height: 30)
                .padding(.horizontal, 6)
                .padding(.bottom, 3)
        }
        .background(.bar)
    }

    private var controls: some View {
        HStack(spacing: 6) {
            transport
            Toggle(isOn: $loop) {
                Image(systemName: "repeat")
            }
            .toggleStyle(.button)
            .controlSize(.small)
            .help(Text("Loop the playback"))
            .accessibilityLabel(Text("Loop"))

            Divider().frame(height: 16)

            labelled("Frame", width: 56) {
                NumberField(text: NumberText.format(model.time.frame), accessibilityName: String(localized: "Frame")) {
                    model.commitFrame($0)
                }
            }
            labelled("Start", width: 52) {
                NumberField(text: String(model.time.start), accessibilityName: String(localized: "Start frame")) {
                    model.commitStart($0)
                }
            }
            labelled("End", width: 52) {
                NumberField(text: String(model.time.end), accessibilityName: String(localized: "End frame")) {
                    model.commitEnd($0)
                }
            }
            labelled("fps", width: 48) {
                NumberField(text: NumberText.format(model.time.fps), accessibilityName: String(localized: "Frames per second")) {
                    model.commitFps($0)
                }
            }

            if let message = model.message {
                Text(message)
                    .font(.caption)
                    .foregroundStyle(.red)
                    .lineLimit(1)
                    .truncationMode(.tail)
            }
            Spacer(minLength: 4)
            if let meter = model.meterText {
                Text(meter)
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
                    .help(Text("The frame rate of the scene, and how many frames a second were really shown"))
            }
        }
        .controlSize(.small)
    }

    private var transport: some View {
        HStack(spacing: 2) {
            button("backward.end.fill", "Go to Start", "Go to the first frame (⌥←)") { model.perform(.goToStart) }
            button("backward.frame.fill", "Step Back", "Back one frame (←)") { model.perform(.stepBack) }
            button(model.isPlaying ? "pause.fill" : "play.fill", model.isPlaying ? "Pause" : "Play", "Play or pause (Space)") {
                model.perform(.playPause)
            }
            button("forward.frame.fill", "Step Forward", "Forward one frame (→)") { model.perform(.stepForward) }
            button("forward.end.fill", "Go to End", "Go to the last frame (⌥→)") { model.perform(.goToEnd) }
        }
    }

    private func button(_ symbol: String, _ name: LocalizedStringKey, _ help: LocalizedStringKey, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: symbol).frame(width: 18, height: 16)
        }
        .buttonStyle(.borderless)
        .help(Text(help))
        .accessibilityLabel(Text(name))
    }

    private func labelled<Field: View>(_ title: LocalizedStringKey, width: CGFloat, @ViewBuilder _ field: () -> Field) -> some View {
        HStack(spacing: 3) {
            Text(title).font(.caption).foregroundStyle(.secondary)
            field().frame(width: width)
        }
    }
}

/// The ruler of the timeline: frame numbers, the range, the playhead and the diamonds. A click or
/// a drag over it moves the playhead and stops the playback, and a diamond drags along it.
struct TimelineRulerView: View {
    @ObservedObject var model: TimelineModel
    @FocusState private var focused: Bool

    /// The height of the row the numbers are in, above the row of diamonds.
    private static let numberRow: CGFloat = 13

    var body: some View {
        GeometryReader { proxy in
            let ruler = TimelineRuler.fitting(start: model.time.start, end: model.time.end, width: proxy.size.width)
            ZStack(alignment: .topLeading) {
                Canvas { context, size in draw(context: &context, size: size, ruler: ruler) }
                    .allowsHitTesting(false)
                ForEach(model.diamonds.filter { $0.frame >= Double(ruler.first) && $0.frame <= Double(ruler.last) }) { diamond in
                    DiamondView(model: model, diamond: diamond, ruler: ruler)
                        .position(x: ruler.x(of: diamond.frame), y: Self.numberRow + 8)
                }
            }
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { drag in
                        focused = true
                        let frame = ruler.frame(atX: drag.location.x)
                        if Double(frame) != model.time.frame || model.isPlaying { model.scrub(to: frame) }
                    }
            )
            .focusable()
            .focused($focused)
            .focusEffectDisabled()
            // The keys of playback, while the ruler has them: a text field of the bar keeps its own.
            .onKeyPress(phases: .down) { press in
                let handled = model.handleKey(
                    code: Self.keyCode(of: press),
                    option: press.modifiers == .option,
                    otherModifiers: !press.modifiers.subtracting(.option).isEmpty
                )
                return handled ? .handled : .ignored
            }
            .accessibilityElement(children: .contain)
            .accessibilityLabel(Text("Timeline"))
        }
    }

    /// The hardware code of the keys the timeline takes, which `TimelineCommand` is written in.
    private static func keyCode(of press: KeyPress) -> UInt16 {
        switch press.key {
        case .space: 49
        case .leftArrow: 123
        case .rightArrow: 124
        default: UInt16.max
        }
    }

    private func draw(context: inout GraphicsContext, size: CGSize, ruler: TimelineRuler) {
        let range = CGRect(
            x: ruler.x(of: Double(model.time.start)), y: 0,
            width: CGFloat(model.time.end - model.time.start) * ruler.pointsPerFrame, height: size.height
        )
        // The range is lighter than the room around it.
        context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(.primary.opacity(0.05)))
        context.fill(Path(range), with: .color(.primary.opacity(0.07)))

        let line = Color.primary.opacity(0.35)
        for frame in ruler.minorFrames() {
            let x = ruler.x(of: Double(frame))
            context.stroke(Path { $0.move(to: CGPoint(x: x, y: Self.numberRow - 2)); $0.addLine(to: CGPoint(x: x, y: Self.numberRow + 2)) }, with: .color(line.opacity(0.6)), lineWidth: 1)
        }
        for frame in ruler.labelledFrames(from: model.time.start) where frame >= ruler.first && frame <= ruler.last {
            let x = ruler.x(of: Double(frame))
            context.stroke(Path { $0.move(to: CGPoint(x: x, y: Self.numberRow - 4)); $0.addLine(to: CGPoint(x: x, y: size.height)) }, with: .color(line.opacity(0.35)), lineWidth: 1)
            let label = Text(String(frame)).font(.system(size: 9).monospacedDigit()).foregroundColor(.secondary)
            context.draw(label, at: CGPoint(x: x + 3, y: 6), anchor: .leading)
        }
        // The end of the range, which a step of ten does not always reach.
        let endX = ruler.x(of: Double(model.time.end))
        context.stroke(Path { $0.move(to: CGPoint(x: endX, y: 0)); $0.addLine(to: CGPoint(x: endX, y: size.height)) }, with: .color(line), lineWidth: 1)
        let startX = ruler.x(of: Double(model.time.start))
        context.stroke(Path { $0.move(to: CGPoint(x: startX, y: 0)); $0.addLine(to: CGPoint(x: startX, y: size.height)) }, with: .color(line), lineWidth: 1)

        // The playhead.
        let x = ruler.x(of: model.time.frame)
        let head = Color.accentColor
        context.stroke(Path { $0.move(to: CGPoint(x: x, y: 0)); $0.addLine(to: CGPoint(x: x, y: size.height)) }, with: .color(head), lineWidth: 1.5)
        context.fill(
            Path { path in
                path.move(to: CGPoint(x: x - 5, y: 0))
                path.addLine(to: CGPoint(x: x + 5, y: 0))
                path.addLine(to: CGPoint(x: x, y: 7))
                path.closeSubpath()
            },
            with: .color(head)
        )
    }
}

/// One diamond of the ruler: the keys of a frame. It drags along the ruler by whole frames, and a
/// right click gives the way the value leaves it and the removal.
struct DiamondView: View {
    @ObservedObject var model: TimelineModel
    let diamond: TimelineDiamond
    let ruler: TimelineRuler
    @State private var dragged: CGFloat = 0
    @State private var dragging = false

    private var atPlayhead: Bool { diamond.frame == model.time.frame }

    var body: some View {
        Diamond()
            .fill(atPlayhead ? Color.orange : Color.orange.opacity(0.75))
            .overlay(Diamond().stroke(Color.black.opacity(0.35), lineWidth: 0.5))
            .frame(width: 10, height: 10)
            .offset(x: dragging ? snapped : 0)
            .frame(width: 18, height: 18)
            .contentShape(Rectangle())
            .highPriorityGesture(
                DragGesture(minimumDistance: 3)
                    .onChanged { drag in
                        dragging = true
                        dragged = drag.translation.width
                    }
                    .onEnded { drag in
                        dragging = false
                        let delta = TimelineKeys.delta(
                            dragging: Double(drag.translation.width), pointsPerFrame: Double(ruler.pointsPerFrame),
                            from: diamond.frame, first: model.time.start, last: model.time.end
                        )
                        dragged = 0
                        model.move(diamond, by: delta)
                    }
            )
            .help(diamond.tooltip)
            .contextMenu {
                Menu("Interpolation") {
                    ForEach(KeyInterpolation.allCases, id: \.self) { interpolation in
                        Button {
                            model.setInterpolation(of: diamond, to: interpolation)
                        } label: {
                            if diamond.keys.allSatisfy({ $0.interpolation == interpolation }) {
                                Label(interpolation.title, systemImage: "checkmark")
                            } else {
                                Text(interpolation.title)
                            }
                        }
                    }
                }
                Button("Delete Keyframe", role: .destructive) { model.remove(diamond) }
            }
            .accessibilityLabel(Text("Keyframe at frame \(NumberText.format(diamond.frame))"))
    }

    /// The offset of the diamond while it drags: a whole number of frames, so it shows where it will land.
    private var snapped: CGFloat {
        let delta = TimelineKeys.delta(
            dragging: Double(dragged), pointsPerFrame: Double(ruler.pointsPerFrame),
            from: diamond.frame, first: model.time.start, last: model.time.end
        )
        return CGFloat(delta) * ruler.pointsPerFrame
    }
}

/// A square turned on its corner.
struct Diamond: Shape {
    func path(in rect: CGRect) -> Path {
        Path { path in
            path.move(to: CGPoint(x: rect.midX, y: rect.minY))
            path.addLine(to: CGPoint(x: rect.maxX, y: rect.midY))
            path.addLine(to: CGPoint(x: rect.midX, y: rect.maxY))
            path.addLine(to: CGPoint(x: rect.minX, y: rect.midY))
            path.closeSubpath()
        }
    }
}
