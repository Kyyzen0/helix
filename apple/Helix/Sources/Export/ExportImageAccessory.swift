import SwiftUI

/// What the person picked in the accessory of the save panel of Export Image.
@MainActor
final class ExportImageChoice: ObservableObject {
    @Published var size = ExportImageSize.viewport
}

/// The line under the file name in the save panel: the size of the picture.
struct ExportImageAccessory: View {
    @ObservedObject var choice: ExportImageChoice

    var body: some View {
        HStack(spacing: 8) {
            Text("Size:")
            Picker("Size", selection: $choice.size) {
                ForEach(ExportImageSize.allCases) { size in Text(size.title).tag(size) }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .fixedSize()
        }
        .padding(.horizontal, 16).padding(.vertical, 10)
    }
}
