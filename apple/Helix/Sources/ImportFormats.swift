import AppKit
import CxxStdlib
import Foundation
import HelixCore
import UniformTypeIdentifiers

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
