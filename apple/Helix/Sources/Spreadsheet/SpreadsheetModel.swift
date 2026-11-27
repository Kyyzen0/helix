import Combine
import Foundation

/// What the spreadsheet asks of the engine. Both calls cook the node only when it is dirty, so
/// asking again for a node that is clean costs a read and not a cook.
@MainActor
protocol SpreadsheetSource: AnyObject {
    /// The counts and the attributes of a node, or why it cannot be shown.
    func report(node: UInt64) -> Result<SpreadsheetReport, SpreadsheetFailure>
    func nodeName(_ node: UInt64) -> String?
    /// A run of one attribute, or nil when it cannot be read.
    func readPage(node: UInt64, owner: SpreadsheetOwner, attribute: String, start: Int, count: Int) -> SpreadsheetPage?
}

struct SpreadsheetFailure: Error, Equatable {
    var message: String
}

/// What the table area shows.
enum SpreadsheetState: Equatable {
    /// There is nothing to show a table of, and why.
    case empty(String)
    /// The node failed to cook, and says why.
    case failed(String)
    case table
}

/// What the spreadsheet shows and does: the node it follows, the owner whose elements are the rows,
/// the columns, and the pages read so far. It holds no engine: it reads through `source`, so the
/// rules of the table (what a column is, when a page is read, when the cache is dropped) can be
/// tested on their own.
@MainActor
final class SpreadsheetModel: ObservableObject {
    @Published var owner = SpreadsheetOwner.points {
        didSet { if owner != oldValue { rebuild() } }
    }
    @Published private(set) var state = SpreadsheetState.empty(String(localized: "No node to show"))
    @Published private(set) var report = SpreadsheetReport()
    @Published private(set) var columns: [SpreadsheetColumn] = []
    @Published private(set) var rowCount = 0
    @Published private(set) var nodeName: String?
    /// Changes whenever what the cells say may have changed, so the table reads them again.
    @Published private(set) var revision = 0

    /// Whether the table is on screen. A table nobody sees does not cook the node it follows, so it
    /// is read again when it comes back into view.
    var isShown = true {
        didSet { if isShown, !oldValue { refresh() } }
    }

    weak var source: SpreadsheetSource?
    private(set) var node: UInt64?
    private(set) var cache = PageCache()
    /// How many pages the table has read from the engine, for the tests.
    private(set) var pagesRead = 0

    nonisolated init() {}

    // MARK: Following a node

    /// Shows the table of a node, or of nothing when it is nil.
    func follow(node: UInt64?) {
        if node != self.node {
            self.node = node
            // Another node's pages are not this node's, even if the numbers of the cook agree.
            cache.reset(generation: 0)
        }
        refresh()
    }

    /// Reads the report of the node again. A node that cooked since the last time has a new
    /// generation, and the pages read before it are dropped.
    func refresh() {
        guard isShown else { return }
        guard let node, let source else {
            show(.empty(String(localized: "No node to show")), name: nil)
            return
        }
        let name = source.nodeName(node)
        switch source.report(node: node) {
        case .failure(let failure):
            show(.failed(failure.message), name: name)
        case .success(let fresh):
            nodeName = name
            let changed = fresh.generation != report.generation || state != .table
            if fresh.generation != cache.generation { cache.reset(generation: fresh.generation) }
            report = fresh
            state = .table
            if changed { rebuild() }
        }
    }

    private func show(_ new: SpreadsheetState, name: String?) {
        let changed = state != new
        nodeName = name
        state = new
        guard changed else { return }
        columns = []
        rowCount = 0
        report = SpreadsheetReport()
        cache.reset(generation: 0)
        revision += 1
    }

    private func rebuild() {
        guard state == .table else { return }
        columns = SpreadsheetColumns.columns(for: owner, in: report)
        rowCount = report.count(of: owner)
        revision += 1
    }

    // MARK: Cells

    /// The text of a cell. A page that has not been read is read now, which is how only the rows
    /// in view ever cost anything.
    func cellText(row: Int, column: SpreadsheetColumn) -> String {
        guard row >= 0, row < rowCount else { return "" }
        switch column.kind {
        case .index:
            return String(row)
        case .primitive:
            return SpreadsheetColumns.primitive(ofRow: row, owner: owner, in: report).map(String.init) ?? ""
        case .component(let attribute, let component):
            if column.mixed { return "mixed" }
            if column.partial { return "–" }
            let (pageNumber, inPage) = cache.locate(row: row)
            if cache.page(attribute: attribute, page: pageNumber) == nil { loadPage(attribute: attribute, page: pageNumber) }
            guard let page = cache.page(attribute: attribute, page: pageNumber) else { return "" }
            return page.text(row: inPage, component: component) ?? ""
        }
    }

    private func loadPage(attribute: String, page number: Int) {
        guard let node, let source else { return }
        let size = cache.pageSize
        guard let page = source.readPage(
            node: node, owner: owner, attribute: attribute, start: number * size, count: size
        ) else { return }
        pagesRead += 1
        if !cache.store(page, attribute: attribute, number: number) {
            // The read cooked the node again, so the cells already drawn are from the cook before.
            // The report is read again and the table redraws, instead of mixing the two.
            DispatchQueue.main.async { [weak self] in self?.refresh(); self?.revision += 1 }
        }
    }

    // MARK: Copy

    /// The selected rows as tab separated text, with the header, or nil when too many are selected.
    func copyText(rows: [Int]) -> String? {
        guard rows.count <= SpreadsheetCopy.maxRows else { return nil }
        return SpreadsheetCopy.text(rows: rows, columns: columns) { cellText(row: $0, column: $1) }
    }
}
