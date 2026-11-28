import CoreGraphics
import Foundation

/// How big the picture File ▸ Export Image… makes: the viewport as it is on screen, or a fixed
/// size to use as it is.
enum ExportImageSize: String, CaseIterable, Identifiable, Sendable {
    case viewport, fullHD, ultraHD

    var id: String { rawValue }

    /// The most a side of a picture may have. A viewport dragged huge, or a bad size, never asks the
    /// GPU for a picture larger than this.
    static let maxSide = 8192

    var title: String {
        switch self {
        case .viewport: String(localized: "Viewport Size")
        case .fullHD: "1920 × 1080" // l10n-ok: a size in pixels
        case .ultraHD: "3840 × 2160" // l10n-ok: a size in pixels
        }
    }

    /// The size in pixels of the picture, given the size the viewport has now, in pixels. A fixed
    /// size does not depend on the window.
    func pixels(viewport: CGSize) -> (width: Int, height: Int) {
        switch self {
        case .fullHD: return (1920, 1080)
        case .ultraHD: return (3840, 2160)
        case .viewport:
            func side(_ value: CGFloat) -> Int {
                guard value.isFinite else { return 1 }
                return min(max(Int(value.rounded()), 1), Self.maxSide)
            }
            return (side(viewport.width), side(viewport.height))
        }
    }
}

enum ExportNaming {
    /// The name the save panel starts with: the title of the window without the extension of the
    /// scene file, and the extension of the format. An untitled scene is called by what is
    /// exported.
    static func suggestedName(title: String, fallback: String, extension ext: String) -> String {
        var base = title.trimmingCharacters(in: .whitespacesAndNewlines)
        if base.lowercased().hasSuffix(".kut") { base = String(base.dropLast(4)) }
        if base.isEmpty { base = fallback }
        // A slash would be read as a folder by the panel.
        base = base.replacingOccurrences(of: "/", with: "-").replacingOccurrences(of: ":", with: "-")
        return base + "." + ext
    }
}
