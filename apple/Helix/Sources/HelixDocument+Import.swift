import CxxStdlib
import Foundation
import HelixCore

/// The file the sheet over the window says is being read.
struct ImportProgress: Equatable {
    let fileName: String
}

/// How the reading of a file for an import ended.
enum ImportRead: Equatable {
    /// The file is in the import cache, so making the node is quick.
    case ready
    /// The person pressed Cancel: nothing is imported.
    case cancelled
    case failed(String)
}

/// An import that does not freeze the window.
///
/// Reading a large model takes seconds (134 MB of glb, two million points: five and a half
/// seconds), and until then the window showed a spinning cursor and no way out. The reading is
/// the slow part, and the engine keeps what it read in a cache of its own, with its own lock. So
/// the app reads the file on another thread (`warmImportCache`, which touches no scene), behind a
/// sheet with a Cancel button, and then makes the node on the main thread, where the engine
/// lives, which finds the file in the cache and takes a moment. The engine is not used from
/// two threads: only its import cache is.
///
/// The read cannot be interrupted. Cancel closes the sheet at once and the answer, when it
/// comes, is thrown away: the scene was not touched, so it is exactly what it was. The file
/// stays in the cache, where the next import of it finds it.
extension HelixDocument {
    /// Imports a file with the sheet over the window, and makes the node it becomes the display
    /// node. Nothing happens when another import is going on.
    func startImport(_ url: URL) {
        Task { await importFileShowingProgress(url) }
    }

    /// The same, for a caller that waits for the end. Returns why it could not be imported, or
    /// nil when it was, or was cancelled.
    @discardableResult
    func importFileShowingProgress(_ url: URL) async -> String? {
        guard isActive, importProgress == nil else { return nil }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        let read = await readForImport(url)
        // The window may have been closed while the file was read: the engine is not ours then.
        guard isActive else { return nil }
        switch read {
        case .ready:
            return importFile(url)
        case .failed(let message):
            presentedError = message
            return message
        case .cancelled:
            return nil
        }
    }

    /// Reads a file into the engine's cache off the main thread, with the sheet shown meanwhile.
    func readForImport(_ url: URL) async -> ImportRead {
        importGeneration += 1
        let generation = importGeneration
        setImportProgress(ImportProgress(fileName: url.lastPathComponent))
        Self.debugLogImport("import sheet shown file=\(url.lastPathComponent)") // l10n-ok: read by launch-smoke.py
        let probe = Self.debugProbeMainThread()
        let started = Date()
        let failure = await Self.readOffMain(url)
        probe?.stop(read: Date().timeIntervalSince(started))
        // Cancel, or another read, took the sheet down: this answer is for nobody.
        guard generation == importGeneration, importProgress != nil else {
            Self.debugLogImport("import sheet cancelled file=\(url.lastPathComponent)") // l10n-ok: read by launch-smoke.py
            return .cancelled
        }
        setImportProgress(nil)
        Self.debugLogImport("import sheet closed file=\(url.lastPathComponent) error=\(failure ?? "none")") // l10n-ok: read by launch-smoke.py
        return failure.map(ImportRead.failed) ?? .ready
    }

    /// Cancels the import the sheet stands for. The scene is as it was.
    func cancelImport() {
        guard importProgress != nil else { return }
        importGeneration += 1
        setImportProgress(nil)
    }

    /// Reads the file on a thread of the pool. The answer is the message of the engine, in words
    /// for a person, or nil.
    private nonisolated static func readOffMain(_ url: URL) async -> String? {
        // With `HELIX_DEBUG_IMPORT_READ_DELAY_MS`, the read takes that much longer, so that
        // `Scripts/live-smoke.py` can disconnect an agent while its import is being read.
        if let text = ProcessInfo.processInfo.environment["HELIX_DEBUG_IMPORT_READ_DELAY_MS"], let milliseconds = Int(text) {
            try? await Task.sleep(for: .milliseconds(milliseconds))
        }
        return await Task.detached(priority: .userInitiated) {
            let status = helix.core.warmImportCache(std.string(url.path))
            return status.ok ? nil : String(engineMessage: status.error)
        }.value
    }

    /// With `HELIX_DEBUG_LAUNCH=1`, says what the sheet of an import does, which
    /// `Scripts/launch-smoke.py` reads.
    private static func debugLogImport(_ line: String) {
        guard ProcessInfo.processInfo.environment["HELIX_DEBUG_LAUNCH"] == "1" else { return }
        debugLog(line)
    }

    /// With `HELIX_DEBUG_LAUNCH=1`, watches the main thread while a file is read, to prove it is
    /// free: the longest time it went without running is written when the read ends.
    private static func debugProbeMainThread() -> MainThreadProbe? {
        guard ProcessInfo.processInfo.environment["HELIX_DEBUG_LAUNCH"] == "1" else { return nil }
        return MainThreadProbe()
    }
}

/// A task on the main actor that wakes every 20 ms and remembers how late it was. A main thread
/// that is blocked for seconds shows as one gap of seconds.
@MainActor
private final class MainThreadProbe {
    private var running = true
    private var longestGap = 0.0

    init() {
        Task { @MainActor [self] in
            while running {
                let before = Date()
                try? await Task.sleep(for: .milliseconds(20))
                longestGap = max(longestGap, Date().timeIntervalSince(before) - 0.02)
            }
        }
    }

    func stop(read: TimeInterval) {
        running = false
        HelixDocument.debugLog("import main thread longest-gap-ms=\(Int(longestGap * 1000)) read-ms=\(Int(read * 1000))") // l10n-ok: read by launch-smoke.py
    }
}
