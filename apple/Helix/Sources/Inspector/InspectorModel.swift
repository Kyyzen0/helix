import Combine
import Foundation

/// One change a person makes in the inspector. The document turns it into calls on the engine.
enum InspectorEdit: Equatable {
    case floats(parameter: String, values: [Int: Double])
    case ints(parameter: String, values: [Int: Int])
    case string(parameter: String, component: Int, value: String)
    case ramp(parameter: String, points: [InspectorRampPoint])
    case setExpression(parameter: String, component: Int, text: String)
    case clearExpression(parameter: String, component: Int)
    case reset(parameter: String)
    /// Keys the value the components have now on the frame the scene sits on.
    case setKeyframe(parameter: String, components: [Int])
    /// Takes off the keys on the frame the scene sits on.
    case removeKeyframe(parameter: String, components: [Int])
    /// Takes off every key, which puts back the value the components held before they were keyed.
    case clearKeyframes(parameter: String, components: [Int])

    var parameter: String {
        switch self {
        case .floats(let parameter, _), .ints(let parameter, _), .string(let parameter, _, _),
             .ramp(let parameter, _), .setExpression(let parameter, _, _),
             .clearExpression(let parameter, _), .reset(let parameter),
             .setKeyframe(let parameter, _), .removeKeyframe(let parameter, _), .clearKeyframes(let parameter, _):
            parameter
        }
    }
}

/// What the inspector asks of the document. Every `apply` is one step of undo; a gesture that
/// goes on (a drag) is bracketed by `beginContinuousEdit` and `endContinuousEdit`, and the
/// `applyLive` calls between them make the scene follow without registering a step each.
@MainActor
protocol InspectorEditing: AnyObject {
    func loadInspector(node: UInt64) -> InspectorNode?
    func attributeSuggestions(node: UInt64, parameter: String) -> [String]
    func apply(_ edit: InspectorEdit, to node: UInt64, named name: String) -> EditOutcome
    func applyLive(_ edit: InspectorEdit, to node: UInt64) -> EditOutcome
    func beginContinuousEdit(named name: String)
    func endContinuousEdit()
    func setDisplay(_ node: UInt64) -> EditOutcome
}

/// An expression being typed on a component, which is not on the node until it is committed.
struct ExpressionDraft: Equatable {
    var parameter: String
    var component: Int
    var text: String
}

/// What the inspector shows and does: the node selected, its parameters and the edit in
/// progress. It holds no engine: the node is a copy, and every edit goes through `editing`.
@MainActor
final class InspectorModel: ObservableObject {
    @Published private(set) var content: InspectorContent = .none
    @Published var expressionDraft: ExpressionDraft?
    /// What is wrong with the last thing typed in a row, by parameter, until the next edit.
    @Published private(set) var rowMessages: [String: String] = [:]
    /// The attributes a row of the attribute style offers, by parameter, once asked for.
    @Published private(set) var suggestions: [String: [String]] = [:]
    /// A refusal of the engine, to show in the inspector.
    @Published private(set) var message: String?

    weak var editing: InspectorEditing?

    private var selection: Set<UInt64> = []
    private var gate = LiveUpdateGate()
    private var pendingLive: (edit: InspectorEdit, node: UInt64)?
    private var flushScheduled = false
    private var gestureActive = false
    /// The time, in seconds, on a clock that only goes forward. Replaced in tests.
    var clock: () -> Double = { ProcessInfo.processInfo.systemUptime }

    nonisolated init() {}

    // MARK: Selection

    var node: InspectorNode? {
        if case .node(let node) = content { return node }
        return nil
    }

    /// Shows the node when exactly one is selected.
    func select(nodes: Set<UInt64>) {
        selection = nodes
        expressionDraft = nil
        rowMessages = [:]
        suggestions = [:]
        message = nil
        reload()
    }

    /// Reads the node again, after an edit or when the engine changed under the panel.
    func reload() {
        switch selection.count {
        case 0:
            content = .none
        case 1:
            guard let id = selection.first, let loaded = editing?.loadInspector(node: id) else {
                content = .none
                return
            }
            if content != .node(loaded) { content = .node(loaded) }
        default:
            content = .many(selection.count)
        }
    }

    var entries: [InspectorEntry] {
        guard let node else { return [] }
        return InspectorTree.build(layout: node.layout, parameters: node.parameters)
    }

    // MARK: Editing

    private func label(of parameter: String) -> String {
        node?.parameters[parameter]?.label ?? parameter
    }

    private func finish(_ outcome: EditOutcome, parameter: String?) {
        if let error = outcome.error {
            message = error
            if let parameter { rowMessages[parameter] = error }
        } else {
            message = nil
            if let parameter { rowMessages[parameter] = nil }
        }
    }

    /// Makes one edit, as one step of undo.
    func commit(_ edit: InspectorEdit) {
        guard let node, let editing else { return }
        let name: String
        switch edit {
        case .setExpression: name = InspectorEditName.editExpression
        case .clearExpression: name = InspectorEditName.clearExpression
        case .reset: name = InspectorEditName.reset(label(of: edit.parameter))
        case .setKeyframe: name = InspectorEditName.setKeyframe
        case .removeKeyframe: name = InspectorEditName.removeKeyframe
        case .clearKeyframes: name = InspectorEditName.clearKeyframes
        default: name = InspectorEditName.change(label(of: edit.parameter))
        }
        finish(editing.apply(edit, to: node.id, named: name), parameter: edit.parameter)
        reload()
    }

    func setFloat(_ parameter: String, component: Int, to value: Double, live: Bool = false) {
        // A component that holds keyframes is written too: the engine sets the key on the current
        // frame, so the change is kept rather than put back by the timeline.
        guard let info = node?.parameters[parameter], component < info.floats.count, value.isFinite else { return }
        let held = info.range?.clamped(value) ?? value
        if live { liveEdit(.floats(parameter: parameter, values: [component: held])) } else {
            commit(.floats(parameter: parameter, values: [component: held]))
        }
    }

    func setInt(_ parameter: String, component: Int, to value: Int, live: Bool = false) {
        guard let info = node?.parameters[parameter], component < info.ints.count else { return }
        let held = info.kind == .toggle ? value : (info.range?.clamped(value) ?? value)
        if live { liveEdit(.ints(parameter: parameter, values: [component: held])) } else {
            commit(.ints(parameter: parameter, values: [component: held]))
        }
    }

    func setString(_ parameter: String, component: Int = 0, to value: String) {
        guard let info = node?.parameters[parameter], component < info.strings.count,
              info.strings[component] != value else { return }
        commit(.string(parameter: parameter, component: component, value: value))
    }

    func setRamp(_ parameter: String, points: [InspectorRampPoint]) {
        commit(.ramp(parameter: parameter, points: points))
    }

    func reset(_ parameter: String) {
        guard let info = node?.parameters[parameter], info.animated.isEmpty else {
            rowMessages[parameter] = String(localized: "This value follows keyframes, which a reset would only be overwritten by. Clear its keyframes first.")
            return
        }
        commit(.reset(parameter: parameter))
    }

    // MARK: Keyframes

    /// The float components of a parameter, which are the ones that can be keyed.
    private func keyable(_ info: InspectorParameter, components: [Int]?) -> [Int] {
        guard info.kind == .float else { return [] }
        let all = Array(0..<info.componentCount)
        return components.map { requested in all.filter(requested.contains) } ?? all
    }

    /// Keys the value the components have now on the current frame. Every component of the
    /// parameter when none are named.
    func setKeyframe(_ parameter: String, components: [Int]? = nil) {
        guard let info = node?.parameters[parameter] else { return }
        let targets = keyable(info, components: components)
        guard !targets.isEmpty else { return }
        commit(.setKeyframe(parameter: parameter, components: targets))
    }

    /// Takes off the keys on the current frame of the components that have one.
    func removeKeyframe(_ parameter: String, components: [Int]? = nil) {
        guard let info = node?.parameters[parameter] else { return }
        let targets = keyable(info, components: components).filter { info.keyed.contains($0) }
        guard !targets.isEmpty else { return }
        commit(.removeKeyframe(parameter: parameter, components: targets))
    }

    /// Takes off every key of the components that hold any, and puts their old values back.
    func clearKeyframes(_ parameter: String, components: [Int]? = nil) {
        guard let info = node?.parameters[parameter] else { return }
        let targets = keyable(info, components: components).filter { info.isAnimated($0) }
        guard !targets.isEmpty else { return }
        commit(.clearKeyframes(parameter: parameter, components: targets))
    }

    func clearExpression(_ parameter: String, component: Int) {
        commit(.clearExpression(parameter: parameter, component: component))
    }

    // MARK: Expressions

    /// Opens the expression of a component for editing. A component with no expression starts
    /// from the value it holds, so the person has something to change.
    func beginExpression(_ parameter: String, component: Int) {
        guard let info = node?.parameters[parameter], info.takesExpression, component < info.componentCount else { return }
        let start: String
        if info.hasExpression(component) {
            start = info.expressions[component]
        } else {
            switch info.kind {
            case .float: start = component < info.floats.count ? NumberText.format(info.floats[component]) : "0"
            case .int, .toggle: start = component < info.ints.count ? NumberText.format(info.ints[component]) : "0"
            default: start = component < info.strings.count ? "\"" + info.strings[component] + "\"" : ""
            }
        }
        expressionDraft = ExpressionDraft(parameter: parameter, component: component, text: start)
    }

    func cancelExpression() { expressionDraft = nil }

    /// Puts the draft on the node. An empty draft changes nothing: removing an expression is
    /// Clear Expression, so that emptying a field by accident does not do it.
    func commitExpression() {
        guard let draft = expressionDraft else { return }
        expressionDraft = nil
        let text = draft.text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }
        if node?.parameters[draft.parameter]?.expressions[safe: draft.component] == text { return }
        commit(.setExpression(parameter: draft.parameter, component: draft.component, text: text))
    }

    // MARK: Gestures

    /// Starts a drag. The scene follows it, and one step of undo is registered when it ends.
    func beginGesture(_ parameter: String) {
        guard !gestureActive, node != nil, let editing else { return }
        gestureActive = true
        gate.reset()
        editing.beginContinuousEdit(named: InspectorEditName.change(label(of: parameter)))
    }

    func endGesture() {
        guard gestureActive else { return }
        flushPending()
        gestureActive = false
        editing?.endContinuousEdit()
        reload()
    }

    private func liveEdit(_ edit: InspectorEdit) {
        guard let node else { return }
        // The panel shows the new value at once, before the engine has caught up.
        applyLocally(edit)
        guard gestureActive else {
            commit(edit)
            return
        }
        switch gate.offer(now: clock()) {
        case .applyNow:
            pendingLive = nil
            performLive(edit, on: node.id)
        case .holdUntil(let time):
            pendingLive = (edit, node.id)
            scheduleFlush(at: time)
        }
    }

    private func performLive(_ edit: InspectorEdit, on node: UInt64) {
        let start = clock()
        finish(editing?.applyLive(edit, to: node) ?? .done, parameter: edit.parameter)
        gate.applied(start: start, end: clock())
    }

    private func flushPending() {
        guard let pending = pendingLive else { return }
        pendingLive = nil
        performLive(pending.edit, on: pending.node)
    }

    private func scheduleFlush(at time: Double) {
        guard !flushScheduled else { return }
        flushScheduled = true
        let delay = max(time - clock(), 0)
        Task { @MainActor [weak self] in
            try? await Task.sleep(for: .seconds(delay))
            guard let self else { return }
            self.flushScheduled = false
            if self.gestureActive { self.flushPending() }
        }
    }

    /// Writes an edit into the copy of the node, so a slider and its field agree while the
    /// engine is held back.
    private func applyLocally(_ edit: InspectorEdit) {
        guard case .node(var current) = content, var info = current.parameters[edit.parameter] else { return }
        switch edit {
        case .floats(_, let values):
            for (component, value) in values where component < info.floats.count { info.floats[component] = value }
        case .ints(_, let values):
            for (component, value) in values where component < info.ints.count { info.ints[component] = value }
        default:
            return
        }
        current.parameters[edit.parameter] = info
        content = .node(current)
    }

    // MARK: Suggestions and display

    func loadSuggestions(_ parameter: String) {
        guard let node, let editing else { return }
        suggestions[parameter] = editing.attributeSuggestions(node: node.id, parameter: parameter)
    }

    func setDisplay() {
        guard let node, let editing else { return }
        finish(editing.setDisplay(node.id), parameter: nil)
        reload()
    }

    func report(_ text: String, for parameter: String) {
        rowMessages[parameter] = text
    }

    func clearMessage(for parameter: String) {
        rowMessages[parameter] = nil
    }
}

extension Array {
    /// The element at an index, or nil past the end.
    subscript(safe index: Int) -> Element? {
        indices.contains(index) ? self[index] : nil
    }
}

/// Whether the inspector column is shown, kept in the user defaults so the menu and every window
/// agree. It is shown until the user hides it.
enum InspectorSetting {
    static let defaultsKey = "inspectorShown"
}
