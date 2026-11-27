import AppKit
import CxxStdlib
import Foundation
import HelixCore
import UniformTypeIdentifiers
import os

/// The 3D file formats Helix imports, which is whatever the engine reads.
///
/// The engine owns the list (`supportedImportExtensions`), so the open panels and the document
/// types follow what it can do. Info.plist declares the same formats to the system, as a Viewer.
enum ImportFormats {
    /// The extensions, lower case and without the dot ("glb", "usdz").
    static let extensions: [String] = {
        var result: [String] = []
        for text in helix.core.supportedImportExtensions() {
            var name = String(engine: text).lowercased()
            if name.hasPrefix(".") { name.removeFirst() }
            result.append(name)
        }
        return result
    }()

    /// The content types of those extensions, which the open panels and the document group take.
    static let contentTypes: [UTType] = {
        var seen = Set<String>()
        return extensions.compactMap { UTType(filenameExtension: $0) }.filter { seen.insert($0.identifier).inserted }
    }()

    /// Whether Helix imports this file, as opposed to opening it as a scene.
    static func canImport(_ url: URL) -> Bool {
        extensions.contains(url.pathExtension.lowercased())
    }
}

/// The files that were opened as a 3D model and wait for the new scene that will hold them.
///
/// The system opens a file by asking for a document. A scene file is read by the document. A 3D
/// file is not a Helix document: it becomes a new scene that imports it, and the document that
/// makes that scene is created on a thread of SwiftUI's choosing, so the request is parked here.
final class ImportRequests: Sendable {
    static let shared = ImportRequests()

    private let pending = OSAllocatedUnfairLock<[URL]>(initialState: [])

    func push(_ url: URL) {
        pending.withLock { $0.append(url) }
    }

    func pop() -> URL? {
        pending.withLock { $0.isEmpty ? nil : $0.removeFirst() }
    }
}

/// Opens a 3D file as a new untitled scene instead of as a document of its own.
///
/// It has to be created before SwiftUI makes its document controller, which is why `main.swift`
/// makes one first: the first controller made is the shared one.
@MainActor
final class HelixDocumentController: NSDocumentController {
    override func openDocument(
        withContentsOf url: URL,
        display displayDocument: Bool,
        completionHandler: @escaping (NSDocument?, Bool, (any Error)?) -> Void
    ) {
        guard ImportFormats.canImport(url) else {
            super.openDocument(withContentsOf: url, display: displayDocument, completionHandler: completionHandler)
            return
        }
        ImportRequests.shared.push(url)
        do {
            let document = try openUntitledDocumentAndDisplay(displayDocument)
            completionHandler(document, false, nil)
        } catch {
            _ = ImportRequests.shared.pop()
            completionHandler(nil, false, error)
        }
    }
}
