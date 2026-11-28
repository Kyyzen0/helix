import CoreGraphics
import Foundation

/// How wide the columns of the table are: what their contents need, then the room left over.
///
/// A fixed width cut "-0.0004318…" short with a third of the table empty to the right. The width
/// of each column is now taken from its header and from a sample of its cells, and what is left of
/// the table's width is shared among the columns that hold numbers.
enum SpreadsheetColumnWidths {
    /// The room a cell keeps around its text, and the room a header keeps for its own.
    static let cellPadding: CGFloat = 14
    static let headerPadding: CGFloat = 20
    /// The widest a column fits itself to: a longer text is cut, and the person can drag the edge.
    static let maximum: CGFloat = 320
    static let minimum: CGFloat = 32

    /// The width a column needs: the widest of its header and its sampled cells, held between the
    /// least and the most.
    static func natural(header: CGFloat, samples: [CGFloat], minimum: CGFloat = SpreadsheetColumnWidths.minimum) -> CGFloat {
        let wanted = max(header + headerPadding, (samples.max() ?? 0) + cellPadding)
        return min(max(wanted, minimum), maximum)
    }

    /// The widths once the room left over is shared out. `available` is the width of the table to
    /// fill, `spacing` what goes between two columns. Only a column marked `flexible` grows, all by
    /// the same amount; when the columns already fill the table, or none can grow, they keep their
    /// natural widths (and the table scrolls sideways).
    static func distribute(natural: [CGFloat], flexible: [Bool], available: CGFloat, spacing: CGFloat) -> [CGFloat] {
        let growing = zip(natural, flexible).filter { $0.1 }.count
        guard growing > 0, natural.count == flexible.count else { return natural }
        let used = natural.reduce(0, +) + spacing * CGFloat(natural.count)
        let spare = available - used
        guard spare > 0 else { return natural }
        let share = (spare / CGFloat(growing)).rounded(.down)
        return zip(natural, flexible).map { $1 ? $0 + share : $0 }
    }
}
