import AppKit
import Combine
import SwiftUI

/// A native table of the elements of the geometry. A table of a million rows is no heavier than one
/// of fifty: the table asks only for the cells in view, and the model reads only the pages those
/// cells are in.
struct SpreadsheetTable: NSViewRepresentable {
    @ObservedObject var model: SpreadsheetModel
    @Environment(\.theme) private var theme
    @Environment(\.interfaceSize) private var size

    func makeCoordinator() -> Coordinator { Coordinator(model: model) }

    func makeNSView(context: Context) -> NSScrollView {
        let table = SpreadsheetTableView()
        table.style = .plain
        table.usesAlternatingRowBackgroundColors = true
        table.allowsMultipleSelection = true
        table.allowsColumnReordering = false
        table.allowsColumnResizing = true
        table.rowHeight = size.spreadsheetRowHeight
        table.intercellSpacing = NSSize(width: 6, height: 0)
        table.gridStyleMask = [.solidVerticalGridLineMask]
        table.gridColor = NSColor.separatorColor.withAlphaComponent(0.4)
        table.columnAutoresizingStyle = .noColumnAutoresizing
        table.dataSource = context.coordinator
        table.delegate = context.coordinator
        table.onCopy = { [weak coordinator = context.coordinator] in coordinator?.copySelection() }
        table.setAccessibilityLabel(String(localized: "Attribute spreadsheet"))
        context.coordinator.table = table

        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.hasHorizontalScroller = true
        scroll.autohidesScrollers = true
        scroll.drawsBackground = false
        context.coordinator.watch(table: table, scroll: scroll)
        context.coordinator.apply(theme.palette)
        context.coordinator.apply(size: size)
        context.coordinator.sync()
        return scroll
    }

    func updateNSView(_ scroll: NSScrollView, context: Context) {
        context.coordinator.model = model
        context.coordinator.apply(theme.palette)
        context.coordinator.apply(size: size)
        context.coordinator.sync()
    }

    @MainActor
    final class Coordinator: NSObject, NSTableViewDataSource, NSTableViewDelegate {
        var model: SpreadsheetModel
        weak var table: SpreadsheetTableView?
        private var shownColumns: [String] = []
        private var shownRevision = -1
        /// The width of the table the columns were last fitted to, and whether the person has
        /// dragged a column edge since: a width they chose is not taken back.
        private var fittedWidth: CGFloat = 0
        private var adjustedByHand = false
        private var fitting = false
        nonisolated(unsafe) private var observers: [NSObjectProtocol] = []

        private var palette: ThemePalette?
        private var size = InterfaceSize.medium
        init(model: SpreadsheetModel) { self.model = model }

        /// Gives the table the colours of the theme. The cells and the header are the system's
        /// text colours, which follow the appearance the theme sets. A theme that follows the
        /// system keeps the alternating rows of AppKit, which a custom theme draws itself
        /// (`StripedRowView`) in a tint of its own background.
        func apply(_ palette: ThemePalette) {
            guard let table, self.palette != palette else { return }
            self.palette = palette
            table.usesAlternatingRowBackgroundColors = palette.followsSystemColors
            table.backgroundColor = palette.followsSystemColors ? .controlBackgroundColor : NSColor(palette.surface)
            table.stripe = palette.followsSystemColors ? nil : NSColor(palette.window.mixed(with: palette.text, by: 0.05))
            table.reloadData()
        }

        /// The row height and the text size of the interface size of Settings.
        func apply(size: InterfaceSize) {
            guard let table, self.size != size || table.rowHeight != size.spreadsheetRowHeight else { return }
            self.size = size
            table.rowHeight = size.spreadsheetRowHeight
            shownColumns = []
            table.reloadData()
        }

        func tableView(_ tableView: NSTableView, rowViewForRow row: Int) -> NSTableRowView? {
            guard let table = tableView as? SpreadsheetTableView, let stripe = table.stripe else { return nil }
            let view = StripedRowView()
            view.stripe = row % 2 == 1 ? stripe : nil
            return view
        }

        /// Brings the table in line with the model: the columns when they changed, and the cells
        /// when anything they say may have.
        func sync() {
            guard let table else { return }
            let ids = model.columns.map(\.id)
            if ids != shownColumns {
                for column in table.tableColumns { table.removeTableColumn(column) }
                for column in model.columns { table.addTableColumn(makeColumn(column)) }
                shownColumns = ids
                shownRevision = -1
                adjustedByHand = false
                fittedWidth = 0
            }
            if shownRevision != model.revision {
                shownRevision = model.revision
                table.reloadData()
            }
            fitColumns()
        }

        /// Sizes the columns to what they hold, and shares the room that is left. Done when the
        /// columns change and when the table changes width, and not again once the person has
        /// dragged an edge, so a width they chose stays.
        private func fitColumns() {
            guard let table, !adjustedByHand, !table.tableColumns.isEmpty else { return }
            let available = table.enclosingScrollView?.contentSize.width ?? 0
            guard available > 0, abs(available - fittedWidth) > 0.5 else { return }
            fittedWidth = available
            let headerFont = NSFont.systemFont(ofSize: size.points(12), weight: .medium)
            let cellFont = NSFont.monospacedDigitSystemFont(ofSize: size.points(13), weight: .regular)
            let sampled = min(model.rowCount, Self.sampleRows)
            var natural: [CGFloat] = []
            var flexible: [Bool] = []
            for tableColumn in table.tableColumns {
                guard let column = model.columns.first(where: { $0.id == tableColumn.identifier.rawValue }) else {
                    natural.append(tableColumn.width)
                    flexible.append(false)
                    continue
                }
                let header = (column.title as NSString).size(withAttributes: [.font: headerFont]).width
                let samples = (0..<sampled).map { row in
                    (model.cellText(row: row, column: column) as NSString).size(withAttributes: [.font: cellFont]).width
                }
                natural.append(SpreadsheetColumnWidths.natural(header: header, samples: samples))
                // The row number and the primitive are narrow by nature: the room goes to the numbers.
                if case .component = column.kind { flexible.append(true) } else { flexible.append(false) }
            }
            let widths = SpreadsheetColumnWidths.distribute(
                natural: natural, flexible: flexible, available: available, spacing: table.intercellSpacing.width
            )
            fitting = true
            for (tableColumn, width) in zip(table.tableColumns, widths) { tableColumn.width = width }
            fitting = false
        }

        /// How many rows of the top of the table are measured to size a column.
        private static let sampleRows = 64

        /// Takes note of a column edge dragged by hand, and of the table changing width (a pane being
        /// resized), which the columns are fitted to again.
        func watch(table: NSTableView, scroll: NSScrollView) {
            observers.append(NotificationCenter.default.addObserver(
                forName: NSTableView.columnDidResizeNotification, object: table, queue: .main
            ) { [weak self] _ in
                MainActor.assumeIsolated {
                    guard let self, !self.fitting else { return }
                    self.adjustedByHand = true
                }
            })
            scroll.postsFrameChangedNotifications = true
            observers.append(NotificationCenter.default.addObserver(
                forName: NSView.frameDidChangeNotification, object: scroll, queue: .main
            ) { [weak self] _ in
                MainActor.assumeIsolated { self?.fitColumns() }
            })
        }

        deinit {
            for observer in observers { NotificationCenter.default.removeObserver(observer) }
        }

        private func makeColumn(_ column: SpreadsheetColumn) -> NSTableColumn {
            let tableColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier(column.id))
            tableColumn.title = column.title
            tableColumn.headerToolTip = column.tooltip
            tableColumn.headerCell.alignment = .right
            tableColumn.headerCell.font = NSFont.systemFont(ofSize: size.points(12), weight: .medium)
            tableColumn.isEditable = false
            switch column.kind {
            case .index:
                tableColumn.width = 64
            case .primitive:
                tableColumn.width = 44
            case .component:
                tableColumn.width = column.type.isWhole ? 72 : 92
            }
            // A long name (material_index) gets the room its header needs.
            tableColumn.width = size.points(tableColumn.width)
            tableColumn.width = max(tableColumn.width, CGFloat(column.title.count) * size.points(8) + 28)
            tableColumn.minWidth = 32
            return tableColumn
        }

        // MARK: Data

        func numberOfRows(in tableView: NSTableView) -> Int { model.rowCount }

        func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
            guard let tableColumn, let column = model.columns.first(where: { $0.id == tableColumn.identifier.rawValue })
            else { return nil }
            let identifier = NSUserInterfaceItemIdentifier("SpreadsheetCell")
            let field: NSTextField
            if let reused = tableView.makeView(withIdentifier: identifier, owner: nil) as? NSTextField {
                field = reused
            } else {
                field = NSTextField(labelWithString: "")
                field.identifier = identifier
                field.alignment = .right
                field.lineBreakMode = .byTruncatingTail
                field.cell?.usesSingleLineMode = true
            }
            field.font = NSFont.monospacedDigitSystemFont(ofSize: size.points(13), weight: .regular)
            field.stringValue = model.cellText(row: row, column: column)
            let muted = column.kind == .index || column.kind == .primitive || column.partial || column.mixed
            field.textColor = muted ? .secondaryLabelColor : .labelColor
            return field
        }

        // MARK: Copy

        func copySelection() {
            guard let table else { return }
            let rows = Array(table.selectedRowIndexes)
            guard let text = model.copyText(rows: rows) else {
                NSSound.beep()
                return
            }
            let pasteboard = NSPasteboard.general
            pasteboard.clearContents()
            pasteboard.setString(text, forType: .string)
        }
    }
}

/// The table, with the command C of the Edit menu wired to the selected rows.
final class SpreadsheetTableView: NSTableView {
    var onCopy: (() -> Void)?
    /// The colour of the odd rows when the theme draws its own stripes.
    var stripe: NSColor?

    @objc func copy(_ sender: Any?) { onCopy?() }

    override func validateUserInterfaceItem(_ item: any NSValidatedUserInterfaceItem) -> Bool {
        if item.action == #selector(copy(_:)) { return !selectedRowIndexes.isEmpty }
        return super.validateUserInterfaceItem(item)
    }
}

/// A row that paints a stripe under its cells, for the themes that draw their own.
final class StripedRowView: NSTableRowView {
    var stripe: NSColor?

    override func drawBackground(in dirtyRect: NSRect) {
        guard let stripe else { return super.drawBackground(in: dirtyRect) }
        stripe.setFill()
        dirtyRect.fill()
    }
}
