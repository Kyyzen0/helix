import SwiftUI

/// The spreadsheet of a document: the node it follows, the choice of which elements are the rows,
/// and the table, or the reason there is none.
struct SpreadsheetView: View {
    @ObservedObject var model: SpreadsheetModel

    var body: some View {
        switch model.state {
        case .empty(let text):
            SpreadsheetPlaceholder(text: text, symbol: "tablecells", isError: false)
        case .failed(let text):
            SpreadsheetPlaceholder(text: text, symbol: "exclamationmark.triangle", isError: true)
        case .table:
            SpreadsheetTable(model: model)
        }
    }
}

/// What goes beside the switch of the bottom pane while the spreadsheet is shown: the node it
/// follows, and the choice of which elements are the rows. The counts are short ("1,03 M"), so the
/// choice fits whatever the size of the geometry.
struct SpreadsheetHeader: View {
    @ObservedObject var model: SpreadsheetModel
    @Environment(\.interfaceSize) private var size

    var body: some View {
        HStack(spacing: size.points(10)) {
            if let name = model.nodeName {
                Label(name, systemImage: "cube")
                    .labelStyle(.titleAndIcon)
                    .lineLimit(1)
                    .truncationMode(.middle)
                    .foregroundStyle(.secondary)
                    .layoutPriority(-1)
            }
            Spacer(minLength: size.points(8))
            Picker("Elements", selection: $model.owner) {
                ForEach(SpreadsheetOwner.allCases) { owner in
                    Text(title(of: owner)).tag(owner)
                }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .fixedSize()
            .disabled(model.state != .table)
            .help(SpreadsheetOwner.explanation)
        }
    }

    private func title(of owner: SpreadsheetOwner) -> String {
        guard model.state == .table else { return owner.title }
        return "\(owner.title) \(CountFormat.abbreviated(UInt64(max(model.report.count(of: owner), 0))))"
    }
}

struct SpreadsheetPlaceholder: View {
    let text: String
    let symbol: String
    let isError: Bool

    var body: some View {
        VStack(spacing: 8) {
            Image(systemName: symbol)
                .font(.system(size: 28))
                .foregroundStyle(isError ? Color.red : Color.secondary.opacity(0.6))
            Text(text)
                .uiFont(.body)
                .foregroundStyle(isError ? Color.red : Color.secondary)
                .multilineTextAlignment(.center)
                .textSelection(.enabled)
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .accessibilityElement(children: .combine)
    }
}
