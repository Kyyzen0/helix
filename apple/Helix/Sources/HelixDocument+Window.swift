import AppKit
import SwiftUI
import UniformTypeIdentifiers

/// Hands the window of a document to it. The document needs it to ask the system to close it
/// and to change what the title bar says; SwiftUI gives a view no window of its own.
struct WindowReader: NSViewRepresentable {
    let document: HelixDocument

    func makeNSView(context: Context) -> NSView { WindowHandoff(document: document) }
    func updateNSView(_ view: NSView, context: Context) {}

    private final class WindowHandoff: NSView {
        weak var document: HelixDocument?

        init(document: HelixDocument) {
            self.document = document
            super.init(frame: .zero)
        }

        required init?(coder: NSCoder) { nil }

        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            guard let window else { return }
            MainActor.assumeIsolated { document?.window = window }
        }
    }
}

extension HelixDocument {
    /// The `NSDocument` the system keeps for this window, which holds the file and the saved state.
    var systemDocument: NSDocument? {
        window.flatMap { NSDocumentController.shared.document(for: $0) }
    }

    /// The window of the document, once the view has handed it over.
    func windowWhenReady() async -> NSWindow? {
        for _ in 0..<40 where window == nil { try? await Task.sleep(for: .milliseconds(50)) }
        return window
    }

    /// How the document is called in its window.
    var displayTitle: String {
        window?.title ?? String(localized: "Untitled")
    }

    /// A 3D file that was opened stands for a new scene that holds the model, not for the model's
    /// file: the document is untitled, Save asks where to put a `.kut`, and the file is never
    /// written to. The system opened it as a document of the file's type, with the file as its
    /// own; this takes the file away and makes the scene a `.kut` that was never saved. The scene
    /// has something in it that is nowhere else, so it counts as edited, and closing it asks.
    func makeUntitledScene() async {
        guard await windowWhenReady() != nil, let document = systemDocument else { return }
        let before = document.fileURL
        document.fileURL = nil
        document.fileType = UTType.helixScene.identifier
        document.displayName = nil
        document.updateChangeCount(.changeDone)
        let now = "fileURL=\(document.fileURL?.path ?? "none") was=\(before?.lastPathComponent ?? "none")" // l10n-ok: read by launch-smoke.py
        Self.debugLogOpened("document title=\(displayTitle) \(now) edited=\(document.isDocumentEdited)") // l10n-ok: read by launch-smoke.py
    }

    private static func debugLogOpened(_ line: String) {
        guard ProcessInfo.processInfo.environment["HELIX_DEBUG_LAUNCH"] == "1" else { return }
        debugLog(line)
    }
}

/// Closes another document the way the Close command does: if it has changes, the person is asked
/// to save them first, and may cancel. The system answers through a selector, so it takes an
/// object.
@MainActor
final class SceneCloser: NSObject {
    private var continuation: CheckedContinuation<Bool, Never>?

    /// Whether the document was closed, which it is not when the person cancelled.
    func close(_ document: NSDocument) async -> Bool {
        await withCheckedContinuation { continuation in
            self.continuation = continuation
            document.canClose(
                withDelegate: self,
                shouldClose: #selector(document(_:shouldClose:contextInfo:)),
                contextInfo: nil
            )
        }
    }

    @objc func document(_ document: NSDocument, shouldClose: Bool, contextInfo: UnsafeMutableRawPointer?) {
        if shouldClose { document.close() }
        continuation?.resume(returning: shouldClose)
        continuation = nil
    }
}
