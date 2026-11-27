import AppKit
import Combine
import SwiftUI

/// A native table of the elements of the geometry. A table of a million rows is no heavier than one
/// of fifty: the table asks only for the cells in view, and the model reads only the pages those
/// cells are in.
struct SpreadsheetTable: NSViewRepresentable {
    @ObservedObject var model: SpreadsheetModel

    func makeCoordinator() -> Coordinator { Coordinator(model: model) }

    func makeNSView(context: Context) -> NSScrollView {
        let table = SpreadsheetTableView()
        table.style = .plain
        table.usesAlternatingRowBackgroundColors = true
        table.allowsMultipleSelection = true
        table.allowsColumnReordering = false
        table.allowsColumnResizing = true
        table.rowHeight = 18
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
        context.coordinator.sync()
        return scroll
    }

    func updateNSView(_ scroll: NSScrollView, context: Context) {
        context.coordinator.model = model
        context.coordinator.sync()
    }

    @MainActor
    final class Coordinator: NSObject, NSTableViewDataSource, NSTableViewDelegate {
        var model: SpreadsheetModel
        weak var table: SpreadsheetTableView?
        private var shownColumns: [String] = []
        private var shownRevision = -1

        init(model: SpreadsheetModel) { self.model = model }

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
            }
            if shownRevision != model.revision {
                shownRevision = model.revision
                table.reloadData()
            }
        }

        private func makeColumn(_ column: SpreadsheetColumn) -> NSTableColumn {
            let tableColumn = NSTableColumn(identifier: NSUserInterfaceItemIdentifier(column.id))
            tableColumn.title = column.title
            tableColumn.headerToolTip = column.tooltip
            tableColumn.headerCell.alignment = .right
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
            tableColumn.width = max(tableColumn.width, CGFloat(column.title.count) * 7 + 28)
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
                field.font = NSFont.monospacedDigitSystemFont(ofSize: 11, weight: .regular)
                field.alignment = .right
                field.lineBreakMode = .byTruncatingTail
                field.cell?.usesSingleLineMode = true
            }
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

    @objc func copy(_ sender: Any?) { onCopy?() }

    override func validateUserInterfaceItem(_ item: any NSValidatedUserInterfaceItem) -> Bool {
        if item.action == #selector(copy(_:)) { return !selectedRowIndexes.isEmpty }
        return super.validateUserInterfaceItem(item)
    }
}
