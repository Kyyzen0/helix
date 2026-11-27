import CxxStdlib
import Foundation
import HelixCore

/// Copies the time and the keys of a node out of the engine.
@MainActor
enum TimelineLoader {
    static func readTime() -> TimelineTime {
        let time = helix.core.timeInfo()
        return TimelineTime(
            frame: Double(time.frame), start: Int(time.startFrame), end: Int(time.endFrame), fps: Double(time.fps)
        )
    }

    /// Every key of every track of a node, with the name each parameter is shown by.
    static func loadKeys(node: UInt64?) -> [TimelineKey] {
        guard let node, node != 0 else { return [] }
        let tracks = Array(helix.core.listKeyframes(node))
        guard !tracks.isEmpty else { return [] }
        var labels: [String: (label: String, count: Int)] = [:]
        let type = helix.core.listNodes().first { $0.id == node }.map { String(engine: $0.type) } ?? ""
        for info in helix.core.listParameters(node) {
            let name = String(engine: info.name)
            labels[name] = (NodeText.label(type, parameter: name, fallback: String(engine: info.label)), Int(info.componentCount))
        }
        var keys: [TimelineKey] = []
        for track in tracks {
            let name = String(engine: track.parameter)
            let known = labels[name]
            for key in track.keys {
                keys.append(TimelineKey(
                    parameter: name, label: known?.label ?? name, component: Int(track.component),
                    componentCount: known?.count ?? 1, frame: Double(key.frame), value: Double(key.value),
                    interpolation: interpolation(key.interpolation)
                ))
            }
        }
        return keys
    }

    /// The whole frames the cook cache holds a cook of the displayed node for.
    static func loadCachedFrames() -> [Int] {
        let node = helix.core.displayNode()
        guard node != 0 else { return [] }
        return helix.core.cachedFrames(node).map { Int($0) }
    }

    static func interpolation(_ value: helix.core.Interpolation) -> KeyInterpolation {
        switch value {
        case .Step: .step
        case .Linear: .linear
        case .Smooth: .smooth
        @unknown default: .smooth
        }
    }

    static func facade(_ value: KeyInterpolation) -> helix.core.Interpolation {
        switch value {
        case .step: .Step
        case .linear: .Linear
        case .smooth: .Smooth
        }
    }
}

/// A timeline that only reads, for the pictures of the command line: there is no document behind
/// it, so nothing it is asked to do changes the scene.
@MainActor
final class ReadOnlyTimelineSource: TimelineEditing {
    func readTime() -> TimelineTime { TimelineLoader.readTime() }
    func loadKeys(node: UInt64?) -> [TimelineKey] { TimelineLoader.loadKeys(node: node) }
    func loadCachedFrames() -> [Int] { TimelineLoader.loadCachedFrames() }
    func moveToFrame(_ frame: Double) {}
    func setRange(start: Int, end: Int) -> EditOutcome { .done }
    func setFps(_ fps: Double) -> EditOutcome { .done }
    func moveKeys(_ keys: [TimelineKey], by delta: Double) -> EditOutcome { .done }
    func setInterpolation(of keys: [TimelineKey], to interpolation: KeyInterpolation) -> EditOutcome { .done }
    func removeKeys(_ keys: [TimelineKey]) -> EditOutcome { .done }
    func playbackChanged(isPlaying: Bool) {}
}
