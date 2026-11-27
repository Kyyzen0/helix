import CxxStdlib
import Foundation
import HelixCore

/// An edit that would make a very large geometry, held until the person says to go on.
struct LargeEditRequest: Identifiable, Equatable {
    let id = UUID()
    let edit: InspectorEdit
    let node: UInt64
    let name: String
    /// The points the node would make.
    let points: Double

    /// The question, with what it will make in whole millions.
    var message: String {
        String(localized: "This will make about \(GeometryLimits.millions(points)) million points and may take a while and use a lot of memory. Continue?")
    }
}

/// What the interface does about edits that ask for more than a machine holds (see
/// `GeometryLimits`): it asks first, and it says so quietly after a cook that left the viewport
/// with a lot. An agent that drives the app over MCP is not held back: it asked in words.
extension HelixDocument {
    /// The points an edit of the inspector would make the node produce, when that is above what
    /// is worth asking about. Nil for every other edit, and for a node that is not a generator.
    func largeEstimate(of edit: InspectorEdit, on node: UInt64) -> Double? {
        let overrides: [String: Double]
        switch edit {
        case .floats(let parameter, let values):
            guard let first = values[0] else { return nil }
            overrides = [parameter: first]
        case .ints(let parameter, let values):
            guard let first = values[0] else { return nil }
            overrides = [parameter: Double(first)]
        default:
            return nil
        }
        guard let info = helix.core.listNodes().first(where: { $0.id == node }) else { return nil }
        return GeometryLimits.largeEstimate(type: String(engine: info.type), overrides: overrides) { name in
            currentNumber(of: name, on: node)
        }
    }

    /// The number a parameter holds, whether it is an integer or a float.
    private func currentNumber(of parameter: String, on node: UInt64) -> Double? {
        let integer = helix.core.getInt(node, std.string(parameter), 0)
        if integer.status.ok { return Double(integer.value) }
        let float = helix.core.getFloat(node, std.string(parameter), 0)
        return float.status.ok ? Double(float.value) : nil
    }

    /// Makes the edit that was asked about.
    func confirmLargeEdit() {
        guard let request = largeEditRequest else { return }
        largeEditRequest = nil
        let outcome = performEdit(named: request.name) { write(request.edit, to: request.node) }
        if let error = outcome.error { presentedError = error }
        inspector.reload()
    }

    /// Leaves the node as it was. The inspector shows its value again.
    func cancelLargeEdit() {
        largeEditRequest = nil
        inspector.reload()
    }

    /// After a cook: a quiet notice when the viewport holds a lot of points, gone when it does not.
    func updateHeavyNotice(pointCount: UInt64) {
        let notice: String? = GeometryLimits.isHeavy(pointCount: pointCount)
            ? String(localized: "The geometry has about \(GeometryLimits.millions(Double(pointCount))) million points. The viewport and the edits may be slow.")
            : nil
        if heavyNotice != notice { heavyNotice = notice }
    }
}
