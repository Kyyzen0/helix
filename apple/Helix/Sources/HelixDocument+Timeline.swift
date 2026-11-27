import Combine
import CxxStdlib
import Foundation
import HelixCore

/// The timeline of the window and the table that follows the same node: moving the playhead,
/// playback, the range and the rate, and the keyframes on the ruler.
///
/// The playhead is a view of the scene and not part of it, so it is not an edit. Moving it makes no
/// step of undo, and an undo leaves it where it was: a restore puts back the frame the captured
/// scene held, and `keepingPlayhead` moves the playhead back to where the person had it. The range
/// and the rate are part of the scene, so changing them is a step, and undoing it puts them back.
extension HelixDocument: TimelineEditing {
    func readTime() -> TimelineTime { TimelineLoader.readTime() }

    func loadKeys(node: UInt64?) -> [TimelineKey] { TimelineLoader.loadKeys(node: node) }

    /// Makes the models of the bottom of the window ready, once the scene is loaded.
    func prepareTimeline() {
        timeline.editing = self
        spreadsheet.source = spreadsheetSource
        spreadsheet.isShown = TimelineSetting.lowerPane == .spreadsheet
        viewport.timeline = timeline
        editor.timeline = timeline
        followedNodeChanged(selection: editor.selectedNodes)
        timeline.reload()
    }

    // MARK: The playhead

    func moveToFrame(_ frame: Double) {
        helix.core.setFrame(Float(frame))
        frameChanged()
    }

    /// Brings everything that shows the scene to the frame it is on: the viewport, the inspector
    /// (an animated value follows the frame) and the table. The node editor is left alone while
    /// playing, since a cook at every frame is not the time to read the whole graph.
    func frameChanged() {
        cookGeometry(reframe: false)
        if timeline.isPlaying { inspector.reload() } else { refreshEditor() }
        spreadsheet.refresh()
    }

    func playbackChanged(isPlaying: Bool) {
        // What was left out while playing is caught up with when it stops.
        if !isPlaying { refreshEditor() }
    }

    /// Runs a change of the scene that has to leave the playhead where it was, such as putting
    /// back a captured scene, which holds the frame it was captured on.
    func keepingPlayhead(_ body: () -> Void) {
        let frame = helix.core.timeInfo().frame
        body()
        helix.core.setFrame(frame)
    }

    // MARK: The node that is followed

    /// The node the keys and the table are of: the one selected when exactly one is, and the one
    /// displayed otherwise.
    func followedNode(selection: Set<UInt64>) -> UInt64? {
        if selection.count == 1, let only = selection.first { return only }
        let display = helix.core.displayNode()
        return display == 0 ? nil : display
    }

    func followedNodeChanged(selection: Set<UInt64>) {
        let node = followedNode(selection: selection)
        timeline.setKeyNode(node)
        spreadsheet.follow(node: node)
    }

    // MARK: Range and rate

    func setRange(start: Int, end: Int) -> EditOutcome {
        performEdit(named: String(localized: "Change Frame Range"), reframe: false) {
            let status = helix.core.setFrameRange(Int32(clamping: start), Int32(clamping: end))
            return status.ok ? .done : .failed(String(engine: status.error))
        }
    }

    func setFps(_ fps: Double) -> EditOutcome {
        performEdit(named: String(localized: "Change Frame Rate"), reframe: false) {
            let status = helix.core.setFps(Float(fps))
            return status.ok ? .done : .failed(String(engine: status.error))
        }
    }

    // MARK: Keys

    func moveKeys(_ keys: [TimelineKey], by delta: Double) -> EditOutcome {
        guard let node = timeline.keyNode else { return .failed(String(localized: "No node is selected.")) }
        let name = keys.count == 1 ? String(localized: "Move Keyframe") : String(localized: "Move Keyframes")
        return performEdit(named: name, reframe: false) {
            var moved: [TimelineKey] = []
            for key in keys {
                let status = helix.core.moveKeyframe(
                    node, std.string(key.parameter), UInt32(key.component), Float(key.frame), Float(key.frame + delta)
                )
                if !status.ok {
                    // The diamond moves whole or not at all: what already moved goes back.
                    for done in moved.reversed() {
                        _ = helix.core.moveKeyframe(
                            node, std.string(done.parameter), UInt32(done.component),
                            Float(done.frame + delta), Float(done.frame)
                        )
                    }
                    return .failed(String(engine: status.error))
                }
                moved.append(key)
            }
            return .done
        }
    }

    func setInterpolation(of keys: [TimelineKey], to interpolation: KeyInterpolation) -> EditOutcome {
        guard let node = timeline.keyNode else { return .failed(String(localized: "No node is selected.")) }
        return performEdit(named: String(localized: "Change Interpolation"), reframe: false) {
            for key in keys {
                let status = helix.core.setKeyframeInterpolation(
                    node, std.string(key.parameter), UInt32(key.component), Float(key.frame),
                    TimelineLoader.facade(interpolation)
                )
                if !status.ok { return .failed(String(engine: status.error)) }
            }
            return .done
        }
    }

    func removeKeys(_ keys: [TimelineKey]) -> EditOutcome {
        guard let node = timeline.keyNode else { return .failed(String(localized: "No node is selected.")) }
        let name = keys.count == 1 ? String(localized: "Delete Keyframe") : String(localized: "Delete Keyframes")
        return performEdit(named: name, reframe: false) {
            for key in keys {
                let status = helix.core.removeKeyframe(node, std.string(key.parameter), UInt32(key.component), Float(key.frame))
                if !status.ok { return .failed(String(engine: status.error)) }
            }
            return .done
        }
    }
}
