import CxxStdlib
import Foundation
import HelixCore

/// A small graph the welcome screen offers to start from. It is built through the facade, into
/// the scene that is open, as one step of undo.
enum StarterExample: CaseIterable, Identifiable, Sendable {
    case cube, waves, terrain

    var id: Self { self }

    var title: String {
        switch self {
        case .cube: String(localized: "Cube")
        case .waves: String(localized: "Waves")
        case .terrain: String(localized: "Terrain")
        }
    }

    var detail: String {
        switch self {
        case .cube: String(localized: "A single node")
        case .waves: String(localized: "A grid bent by a sine wave")
        case .terrain: String(localized: "A grid raised into hills by noise")
        }
    }

    var symbol: String {
        switch self {
        case .cube: "cube"
        case .waves: "water.waves"
        case .terrain: "mountain.2"
        }
    }

    /// Makes the nodes, wires them and shows the last one. Returns why it failed, or nil.
    func build() -> String? {
        func node(_ type: String, _ y: Float) -> helix.core.NodeId? {
            let created = helix.core.createNode(std.string(type), std.string(""), 0, y)
            return created.status.ok ? created.id : nil
        }
        func fail(_ status: helix.core.Status) -> String? {
            status.ok ? nil : String(engine: status.error)
        }
        switch self {
        case .cube:
            guard let cube = node("helix::cube", 0) else { return String(localized: "The example could not be built.") }
            return fail(helix.core.setDisplayNode(cube))
        case .waves, .terrain:
            guard let grid = node("helix::grid", 0) else { return String(localized: "The example could not be built.") }
            let size: Float = self == .waves ? 12 : 20
            let divisions: Int64 = self == .waves ? 60 : 80
            for status in [
                helix.core.setInt(grid, "rows", 0, divisions), helix.core.setInt(grid, "columns", 0, divisions),
                helix.core.setFloat(grid, "size", 0, size), helix.core.setFloat(grid, "size", 1, size),
            ] {
                if let message = fail(status) { return message }
            }
            if self == .terrain {
                guard let mountain = node("helix::mountain", 120) else { return String(localized: "The example could not be built.") }
                if let message = fail(helix.core.connectNodes(grid, 0, mountain, 0)) { return message }
                return fail(helix.core.setDisplayNode(mountain))
            }
            guard let wave = node("helix::sineWave", 120), let lift = node("helix::transform", 240) else {
                return String(localized: "The example could not be built.")
            }
            for status in [
                helix.core.setInt(wave, "radial", 0, 1), helix.core.setFloat(wave, "frequency", 0, 0.6),
                helix.core.setFloat(wave, "amplitude", 0, 0.8), helix.core.setFloat(lift, "translate", 1, 0.5),
                helix.core.connectNodes(grid, 0, wave, 0), helix.core.connectNodes(wave, 0, lift, 0),
                helix.core.setDisplayNode(lift),
            ] {
                if let message = fail(status) { return message }
            }
            return nil
        }
    }
}
