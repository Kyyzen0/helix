import CoreGraphics
import Foundation

/// What copying nodes puts on the pasteboard, and where pasted nodes go.
///
/// The engine makes the copy (`copyNodes`): a text, JSON that starts with a format tag. It goes
/// on the pasteboard twice, under a type of Helix's own (so a paste inside Helix knows what it
/// holds) and as plain text (so the copy can be kept in a note and pasted back).
enum NodeClipboard {
    /// The pasteboard type of a copy of nodes.
    static let typeIdentifier = "app.helix.nodes" // l10n-ok: a pasteboard type

    /// Whether a text starts like a copy of nodes. A cheap look before the engine reads the whole
    /// text, so that a paste of an ordinary sentence says so at once.
    static func looksLikeCopy(_ text: String) -> Bool {
        let head = text.prefix(256)
        return head.contains("\"helixNodes\"") && head.contains("helix.nodes") // l10n-ok: the format tag
    }

    /// How far a copy that has no place of its own moves from the nodes it was copied from, so
    /// that it does not sit exactly on them.
    static let step = CGPoint(x: 40, y: 40)

    /// How near two cards have to be to count as the same spot.
    private static let sameSpot: CGFloat = 8

    /// The move that takes pasted nodes from where they were copied to where they go.
    ///
    /// With a `target` (the pointer over the canvas) the middle of the nodes goes there. Without
    /// one they go one step down and to the right of where they were. Either way the nodes are
    /// moved on by a step while one of them would land on a card that is there already, so that
    /// pasting twice does not pile the nodes up.
    static func offset(for positions: [CGPoint], occupied: [CGPoint], target: CGPoint?) -> CGPoint {
        guard !positions.isEmpty else { return .zero }
        var delta = CGPoint.zero
        if let target {
            let xs = positions.map(\.x), ys = positions.map(\.y)
            let middle = CGPoint(x: (xs.min()! + xs.max()!) / 2, y: (ys.min()! + ys.max()!) / 2)
            delta = CGPoint(x: target.x - middle.x, y: target.y - middle.y)
        } else {
            delta = step
        }
        for _ in 0..<64 {
            let lands = positions.contains { position in
                occupied.contains {
                    abs($0.x - (position.x + delta.x)) < sameSpot && abs($0.y - (position.y + delta.y)) < sameSpot
                }
            }
            guard lands else { break }
            delta = CGPoint(x: delta.x + step.x, y: delta.y + step.y)
        }
        return delta
    }
}
