import CoreGraphics
import Foundation

/// Moving the selection from card to card with the arrow keys, which is how a person who does not
/// use a pointer reads a graph.
enum NodeNavigation {
    enum Direction: Equatable, Sendable {
        case left, right, up, down

        /// The direction an arrow key stands for: the key codes of the four arrows.
        init?(keyCode: UInt16) {
            switch keyCode {
            case 123: self = .left
            case 124: self = .right
            case 125: self = .down
            case 126: self = .up
            default: return nil
            }
        }

        /// How far a vector goes along the direction, and how far it strays from it.
        fileprivate func split(_ delta: CGPoint) -> (along: CGFloat, across: CGFloat) {
            switch self {
            case .right: (delta.x, abs(delta.y))
            case .left: (-delta.x, abs(delta.y))
            case .down: (delta.y, abs(delta.x))
            case .up: (-delta.y, abs(delta.x))
            }
        }
    }

    /// The card to go to from one card, or nil when no card lies that way. A card counts when its
    /// centre is ahead along the direction; the nearest is the one with the least distance ahead
    /// plus twice the distance off the line, so a card straight ahead wins over a nearer one that
    /// is mostly to the side. Ties go to the lower id, so the answer is the same every time.
    static func nearest(from source: UInt64, toward direction: Direction, in nodes: [EditorNode]) -> UInt64? {
        guard let origin = nodes.first(where: { $0.id == source })?.position else { return nil }
        var best: (id: UInt64, score: CGFloat)?
        for node in nodes where node.id != source {
            let (along, across) = direction.split(CGPoint(x: node.position.x - origin.x, y: node.position.y - origin.y))
            guard along > 0.5 else { continue }
            let score = along + 2 * across
            if let current = best, score > current.score || (score == current.score && node.id > current.id) { continue }
            best = (node.id, score)
        }
        return best?.id
    }

    /// Where the selection goes on an arrow key. From a selection of several cards the one that
    /// reaches furthest in the direction is the one the move starts from. With nothing selected
    /// the first arrow lands on the displayed node, or on the top left card when none is.
    static func target(
        from selection: Set<UInt64>, toward direction: Direction, in nodes: [EditorNode], display: UInt64?
    ) -> UInt64? {
        let selected = nodes.filter { selection.contains($0.id) }
        guard !selected.isEmpty else {
            if let display, nodes.contains(where: { $0.id == display }) { return display }
            return nodes.min { ($0.position.y, $0.position.x, $0.id) < ($1.position.y, $1.position.x, $1.id) }?.id
        }
        let anchor = selected.max { first, second in
            let a = direction.split(first.position).along, b = direction.split(second.position).along
            return a == b ? first.id > second.id : a < b
        }
        return anchor.flatMap { nearest(from: $0.id, toward: direction, in: nodes) }
    }
}

/// What a card says to VoiceOver: its name and type, and what is true of it now.
enum NodeAccessibilityText {
    static func label(name: String, typeLabel: String) -> String {
        "\(name), \(typeLabel)"
    }

    /// The state of a card, most urgent first: its error (or the number of its warnings), whether it
    /// is the node shown in the viewport, whether it is selected. Empty when none applies.
    static func value(error: String?, warningCount: Int, isDisplay: Bool, isSelected: Bool) -> String {
        var parts: [String] = []
        if let error { parts.append(String(localized: "Error: \(error)")) }
        else if warningCount > 0 { parts.append(String(localized: "Warnings: \(warningCount)")) }
        if isDisplay { parts.append(String(localized: "Displayed")) }
        if isSelected { parts.append(String(localized: "Selected")) }
        return parts.joined(separator: ", ")
    }
}
