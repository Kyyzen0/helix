import CxxStdlib
import Foundation
import HelixCore

/// The edits of the inspector, each one a step of undo, or part of one while a drag goes on.
extension HelixDocument: InspectorEditing {
    func loadInspector(node: UInt64) -> InspectorNode? {
        guard isActive else { return nil }
        return InspectorLoader.load(node: node)
    }

    func attributeSuggestions(node: UInt64, parameter: String) -> [String] {
        guard isActive else { return [] }
        let result = helix.core.listAttributeSuggestions(node, std.string(parameter))
        // A node with nothing wired in, or whose input failed, has nothing to suggest, which
        // is not worth an alert: the field takes any name.
        guard result.status.ok else { return [] }
        return result.names.map { String(engine: $0) }
    }

    func apply(_ edit: InspectorEdit, to node: UInt64, named name: String) -> EditOutcome {
        // A value that would make a geometry too large to be an accident is asked about first.
        if let points = largeEstimate(of: edit, on: node) {
            largeEditRequest = LargeEditRequest(edit: edit, node: node, name: name, points: points)
            return .waiting
        }
        return performEdit(named: name) { write(edit, to: node) }
    }

    func applyLive(_ edit: InspectorEdit, to node: UInt64) -> EditOutcome {
        guard isActive else { return .failed(String(localized: "This scene is closed.")) }
        // A drag does not stop to ask: the value that is too large is held, and asked about when
        // the drag ends, with the scene still as it was.
        if let points = largeEstimate(of: edit, on: node) {
            heldLargeEdit = LargeEditRequest(
                edit: edit, node: node, name: continuousEdit.active?.name ?? InspectorEditName.change(edit.parameter), points: points
            )
            return .done
        }
        heldLargeEdit = nil
        let outcome = write(edit, to: node)
        if outcome.error == nil {
            cookGeometry()
            // The table follows a drag too, when it is on screen.
            spreadsheet.refresh()
            // An edit drops what the cook cache held, which the ruler shows.
            timeline.refreshCache()
        }
        return outcome
    }

    // setDisplay(_:) is the node editor's, which the inspector shares.

    // MARK: A drag as one step

    func beginContinuousEdit(named name: String) {
        guard isActive else { return }
        timeline.pause()
        let before = helix.core.captureScene()
        // An edit that cannot be undone is not made, so a drag whose start cannot be kept does
        // not start either: the live edits that follow are refused.
        guard before.status.ok else { return }
        continuousEdit.begin(named: name, before: String(engineBytes: before.data))
    }

    func endContinuousEdit() {
        guard continuousEdit.isActive else { return }
        defer {
            if let held = heldLargeEdit {
                heldLargeEdit = nil
                largeEditRequest = held
            }
        }
        let after = helix.core.captureScene()
        let afterText = after.status.ok ? String(engineBytes: after.data) : nil
        if let registration = continuousEdit.end(after: afterText), let afterText {
            registerUndo(named: registration.name, restoring: registration.before, otherwise: afterText)
        }
        cookGeometry()
        refreshEditor()
    }

    /// Puts the scene back as it was when the drag began, and registers nothing.
    func cancelContinuousEdit() {
        heldLargeEdit = nil
        guard let before = continuousEdit.cancel() else { return }
        let paths = selectedPathsBeforeRestore()
        var status = helix.core.Status()
        keepingPlayhead { status = helix.core.restoreScene(std.string(before)) }
        if !status.ok { presentedError = String(engineMessage: status.error) }
        cookGeometry(reframe: false)
        reselectAfterRestore(paths: paths)
    }

    // MARK: Writing

    func write(_ edit: InspectorEdit, to node: UInt64) -> EditOutcome {
        func outcome(_ status: helix.core.Status) -> EditOutcome {
            status.ok ? .done : .failed(String(engineMessage: status.error))
        }
        switch edit {
        case .floats(let parameter, let values):
            for (component, value) in values.sorted(by: { $0.key < $1.key }) {
                let status = helix.core.setFloat(node, std.string(parameter), UInt32(component), Float(value))
                if !status.ok { return outcome(status) }
            }
            return .done
        case .ints(let parameter, let values):
            for (component, value) in values.sorted(by: { $0.key < $1.key }) {
                let status = helix.core.setInt(node, std.string(parameter), UInt32(component), Int64(value))
                if !status.ok { return outcome(status) }
            }
            return .done
        case .string(let parameter, let component, let value):
            return outcome(helix.core.setString(node, std.string(parameter), UInt32(component), std.string(value)))
        case .ramp(let parameter, let points):
            var list = helix.core.RampPointList()
            for point in points {
                var entry = helix.core.RampPoint()
                entry.position = Float(point.position)
                entry.value = Float(point.value)
                entry.interpolation = std.string(point.interpolation)
                list.push_back(entry)
            }
            return outcome(helix.core.setRamp(node, std.string(parameter), list))
        case .setExpression(let parameter, let component, let text):
            return outcome(helix.core.setExpression(node, std.string(parameter), UInt32(component), std.string(text)))
        case .clearExpression(let parameter, let component):
            return outcome(helix.core.clearExpression(node, std.string(parameter), UInt32(component)))
        case .reset(let parameter):
            return outcome(helix.core.resetParameter(node, std.string(parameter)))
        case .setKeyframe(let parameter, let components):
            let frame = helix.core.timeInfo().frame
            for component in components {
                let status = helix.core.setKeyframeAtCurrentValue(node, std.string(parameter), UInt32(component), frame)
                if !status.ok { return outcome(status) }
            }
            return .done
        case .removeKeyframe(let parameter, let components):
            let frame = helix.core.timeInfo().frame
            for component in components {
                let status = helix.core.removeKeyframe(node, std.string(parameter), UInt32(component), frame)
                if !status.ok { return outcome(status) }
            }
            return .done
        case .clearKeyframes(let parameter, let components):
            for component in components {
                let status = helix.core.clearKeyframes(node, std.string(parameter), UInt32(component))
                if !status.ok { return outcome(status) }
            }
            return .done
        }
    }
}
