import Foundation

// The pieces of the spreadsheet that work on plain values: the columns an owner has, how a number
// is written in a cell, the cache of the pages read so far and the text a copy makes. Nothing here
// touches the engine or a view, so each rule can be tested on its own.

/// Which elements of the geometry the rows of the table are.
enum SpreadsheetOwner: String, CaseIterable, Identifiable, Sendable {
    case points, vertices, faces, primitives

    var id: String { rawValue }

    var title: String {
        switch self {
        case .points: String(localized: "Points")
        case .vertices: String(localized: "Vertices")
        case .faces: String(localized: "Faces")
        case .primitives: String(localized: "Primitives")
        }
    }

    /// Reads the name the command line gives an owner, in the singular or the plural.
    init?(name: String) {
        switch name.lowercased() {
        case "point", "points": self = .points
        case "vertex", "vertices": self = .vertices
        case "face", "faces": self = .faces
        case "primitive", "primitives": self = .primitives
        default: return nil
        }
    }
}

/// What an attribute holds per element, as the facade says.
enum SpreadsheetValueType: Equatable, Sendable {
    case int, float, vector3, bool, matrix4

    var name: String {
        switch self {
        case .int: "int"
        case .float: "float"
        case .vector3: "vector3"
        case .bool: "bool"
        case .matrix4: "matrix4"
        }
    }

    /// Whether the numbers are whole, and are written as such.
    var isWhole: Bool { self == .int || self == .bool }
}

/// One attribute of the geometry, as the report of a node lists it.
struct SpreadsheetAttribute: Equatable, Sendable {
    var owner: SpreadsheetOwner
    var name: String
    var type: SpreadsheetValueType
    var componentCount: Int
    var elementCount: Int
    /// Different primitives carry the name with different types, which the facade will not read.
    var mixedTypes = false
}

/// Where one primitive's elements start among the elements of the whole output.
struct SpreadsheetPrimitiveRange: Equatable, Sendable {
    var pointStart = 0, pointCount = 0
    var vertexStart = 0, vertexCount = 0
    var faceStart = 0, faceCount = 0
}

/// What a node produced, as the table needs to know it.
struct SpreadsheetReport: Equatable, Sendable {
    var generation: UInt64 = 0
    var pointCount = 0
    var vertexCount = 0
    var faceCount = 0
    var primitiveCount = 0
    var attributes: [SpreadsheetAttribute] = []
    var primitives: [SpreadsheetPrimitiveRange] = []

    func count(of owner: SpreadsheetOwner) -> Int {
        switch owner {
        case .points: pointCount
        case .vertices: vertexCount
        case .faces: faceCount
        case .primitives: primitiveCount
        }
    }
}

/// One column of the table.
struct SpreadsheetColumn: Equatable, Identifiable, Sendable {
    enum Kind: Equatable, Sendable {
        /// The number of the row.
        case index
        /// The primitive the row belongs to.
        case primitive
        /// One component of an attribute.
        case component(attribute: String, component: Int)
    }

    var kind: Kind
    var id: String
    /// What the header says, such as "P.x" or "m.12".
    var title: String
    /// What the pointer shows on the header: the attribute, its type and where it lives.
    var tooltip: String
    var type: SpreadsheetValueType
    /// The attribute is on some primitives only, so its values do not line up with the rows.
    var partial = false
    /// The facade will not read the attribute, since its type differs between primitives.
    var mixed = false
}

enum SpreadsheetColumns {
    /// The short name a header uses. The position of a point is "P", as in the tools the table
    /// looks like, and the tooltip carries the full name.
    static func shortName(_ name: String) -> String {
        name == "Position" ? "P" : name
    }

    /// The suffix of one component: x, y, z for a vector, a number for a matrix, none for a scalar.
    static func suffix(component: Int, of attribute: SpreadsheetAttribute) -> String {
        switch attribute.type {
        case .vector3: return "." + (["x", "y", "z"][safe: component] ?? String(component))
        case .matrix4: return "." + String(component)
        default: return attribute.componentCount > 1 ? "." + String(component) : ""
        }
    }

    /// The columns of the table of an owner: the row number, the primitive when there are several,
    /// and one column per component of every attribute the owner has, in the order the report gives.
    static func columns(for owner: SpreadsheetOwner, in report: SpreadsheetReport) -> [SpreadsheetColumn] {
        var columns = [SpreadsheetColumn(
            kind: .index, id: "#", title: "#",
            tooltip: String(localized: "The number of the row"), type: .int
        )]
        if owner != .primitives, report.primitiveCount > 1 {
            columns.append(SpreadsheetColumn(
                kind: .primitive, id: "prim", title: String(localized: "prim"),
                tooltip: String(localized: "The primitive the row belongs to"), type: .int
            ))
        }
        let total = report.count(of: owner)
        for attribute in report.attributes where attribute.owner == owner {
            let partial = attribute.elementCount != total
            for component in 0..<max(attribute.componentCount, 1) {
                let title = shortName(attribute.name) + suffix(component: component, of: attribute)
                var tooltip = "\(attribute.name): \(attribute.type.name)"
                if attribute.componentCount > 1 { tooltip += ", \(attribute.componentCount) components" }
                if partial {
                    tooltip += "\n" + String(localized: "On some primitives only, so its values are not shown against the rows.")
                }
                if attribute.mixedTypes {
                    tooltip += "\n" + String(localized: "The primitives give it different types, so it cannot be read.")
                }
                columns.append(SpreadsheetColumn(
                    kind: .component(attribute: attribute.name, component: component),
                    id: "\(attribute.name).\(component)",
                    title: title, tooltip: tooltip, type: attribute.type,
                    partial: partial, mixed: attribute.mixedTypes
                ))
            }
        }
        return columns
    }

    /// The primitive a row of a point, vertex or face table belongs to, by binary search over the
    /// start of each primitive. Nil when the report has no primitive for it.
    static func primitive(ofRow row: Int, owner: SpreadsheetOwner, in report: SpreadsheetReport) -> Int? {
        guard owner != .primitives else { return row }
        func start(_ range: SpreadsheetPrimitiveRange) -> Int {
            switch owner {
            case .points: range.pointStart
            case .vertices: range.vertexStart
            default: range.faceStart
            }
        }
        func count(_ range: SpreadsheetPrimitiveRange) -> Int {
            switch owner {
            case .points: range.pointCount
            case .vertices: range.vertexCount
            default: range.faceCount
            }
        }
        // The last primitive that starts at or before the row. Primitives with no elements share
        // their start with the next one, so the last of them is the one that holds the row.
        var low = 0
        var high = report.primitives.count - 1
        var found: Int?
        while low <= high {
            let middle = (low + high) / 2
            if start(report.primitives[middle]) <= row {
                found = middle
                low = middle + 1
            } else {
                high = middle - 1
            }
        }
        guard let candidate = found else { return nil }
        let range = report.primitives[candidate]
        return row < start(range) + count(range) ? candidate : nil
    }
}

/// How a value is written in a cell.
enum SpreadsheetFormat {
    /// Six significant digits, with no trailing zeros, and an exponent where a magnitude would take
    /// a long run of digits. A value that is whole is written without a point.
    static func float(_ value: Float) -> String {
        guard value.isFinite else { return value.isNaN ? "nan" : (value < 0 ? "-inf" : "inf") }
        if value == 0 { return "0" }
        let text = String(format: "%.6g", Double(value))
        return text == "-0" ? "0" : text
    }

    static func int(_ value: Int64) -> String { String(value) }

    static func bool(_ value: Int64) -> String { value != 0 ? "true" : "false" }
}

/// The numbers of a run of rows of one attribute, as the facade returns them.
struct SpreadsheetPage: Equatable, Sendable {
    var generation: UInt64
    var totalCount: Int
    var componentCount: Int
    var type: SpreadsheetValueType
    /// Floats, `componentCount` to a row, for every type but the whole numbers.
    var floats: [Float] = []
    /// Whole numbers and booleans, `componentCount` to a row.
    var ints: [Int64] = []

    var rowCount: Int {
        let width = max(componentCount, 1)
        return type.isWhole ? ints.count / width : floats.count / width
    }

    /// The cell text of one component of a row of the page.
    func text(row: Int, component: Int) -> String? {
        let index = row * max(componentCount, 1) + component
        guard row >= 0, component >= 0, component < max(componentCount, 1) else { return nil }
        switch type {
        case .int:
            return ints.indices.contains(index) ? SpreadsheetFormat.int(ints[index]) : nil
        case .bool:
            return ints.indices.contains(index) ? SpreadsheetFormat.bool(ints[index]) : nil
        default:
            return floats.indices.contains(index) ? SpreadsheetFormat.float(floats[index]) : nil
        }
    }
}

/// The pages of the attributes read so far, so a row that scrolls back into view is not read again.
///
/// A page is only good for the cook it came from: when the node cooks again its numbers may all
/// have changed, so a page that arrives with another generation than the cache holds empties it.
struct PageCache: Equatable {
    /// A page is the values of one attribute of one owner: a point attribute and a face attribute
    /// can share a name and hold different values, so the owner is part of what names a page.
    struct Key: Hashable, Sendable {
        var owner: SpreadsheetOwner
        var attribute: String
        var page: Int
    }

    let pageSize: Int
    /// The most pages kept. A page of a few hundred rows of a vector is a few kilobytes, so this
    /// holds a long scroll without growing with the size of the mesh.
    let capacity: Int

    private(set) var generation: UInt64 = 0
    private var pages: [Key: SpreadsheetPage] = [:]
    private var order: [Key] = []

    init(pageSize: Int = 512, capacity: Int = 512) {
        self.pageSize = pageSize
        self.capacity = capacity
    }

    var count: Int { pages.count }

    func page(owner: SpreadsheetOwner, attribute: String, page: Int) -> SpreadsheetPage? {
        pages[Key(owner: owner, attribute: attribute, page: page)]
    }

    /// Forgets every page and takes the cook they now have to come from.
    mutating func reset(generation: UInt64) {
        pages.removeAll(keepingCapacity: true)
        order.removeAll(keepingCapacity: true)
        self.generation = generation
    }

    /// Keeps a page that was read. Returns false when the page came from another cook than the
    /// ones before it: the cache was emptied, and the page is the first of the new cook.
    @discardableResult
    mutating func store(_ page: SpreadsheetPage, owner: SpreadsheetOwner, attribute: String, number: Int) -> Bool {
        let fresh = page.generation == generation
        if !fresh { reset(generation: page.generation) }
        let key = Key(owner: owner, attribute: attribute, page: number)
        if pages[key] == nil {
            order.append(key)
            if order.count > capacity {
                let evicted = order.removeFirst()
                pages[evicted] = nil
            }
        }
        pages[key] = page
        return fresh
    }

    /// The page a row is in, and the row inside it.
    func locate(row: Int) -> (page: Int, row: Int) {
        (row / pageSize, row % pageSize)
    }
}

/// The text of a copy: the header and the selected rows, separated by tabs, as a spreadsheet or a
/// text editor takes it.
enum SpreadsheetCopy {
    /// The most rows a copy takes, so selecting a million rows does not read them all.
    static let maxRows = 100_000

    static func text(rows: [Int], columns: [SpreadsheetColumn], cell: (Int, SpreadsheetColumn) -> String) -> String {
        var lines = [columns.map(\.title).joined(separator: "\t")]
        for row in rows {
            lines.append(columns.map { cell(row, $0) }.joined(separator: "\t"))
        }
        return lines.joined(separator: "\n") + "\n"
    }
}
