import SwiftUI

/// The spreadsheet of a document: the node it follows, the choice of which elements are the rows,
/// and the table, or the reason there is none.
struct SpreadsheetView: View {
    @ObservedObject var model: SpreadsheetModel

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()
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

    private var header: some View {
        HStack(spacing: 10) {
            if let name = model.nodeName {
                Label(name, systemImage: "cube")
                    .labelStyle(.titleAndIcon)
                    .lineLimit(1)
                    .foregroundStyle(.secondary)
            }
            Spacer(minLength: 8)
            Picker("Elements", selection: $model.owner) {
                ForEach(SpreadsheetOwner.allCases) { owner in
                    Text(title(of: owner)).tag(owner)
                }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .fixedSize()
            .disabled(model.state != .table)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 5)
    }

    private func title(of owner: SpreadsheetOwner) -> String {
        guard model.state == .table else { return owner.title }
        return String(localized: "\(owner.title) \(model.report.count(of: owner).formatted())")
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
                .foregroundStyle(isError ? Color.red : Color.secondary)
                .multilineTextAlignment(.center)
                .textSelection(.enabled)
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .accessibilityElement(children: .combine)
    }
}
