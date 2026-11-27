import Foundation

/// The language of the app: the one macOS uses, or one the user chose in the Settings.
///
/// The choice is the `AppleLanguages` override of the app's own user defaults, which is what
/// Foundation reads when the app starts to pick its strings. A change therefore applies at the
/// next launch, and "System" removes the override.
enum AppLanguage: String, CaseIterable, Identifiable, Sendable {
    case system
    case english = "en"
    case french = "fr"

    static let overrideKey = "AppleLanguages"

    var id: String { rawValue }

    /// The language codes the override can hold, which are the localizations the app ships.
    static let shipped: [String] = ["en", "fr"]

    /// The name of the language, in that language, as every language picker writes it. "System"
    /// is the only one that is translated.
    var title: String {
        switch self {
        case .system: String(localized: "System")
        case .english: "English" // l10n-ok: a language is written in itself
        case .french: "Français" // l10n-ok: a language is written in itself
        }
    }

    /// The choice kept in the defaults of the app, `system` when there is no override or it is
    /// not one of ours. `domain` is the name of the defaults domain of the app.
    static func stored(in defaults: UserDefaults = .standard, domain: String? = Bundle.main.bundleIdentifier) -> AppLanguage {
        guard let domain, let languages = defaults.persistentDomain(forName: domain)?[overrideKey] as? [String],
              let first = languages.first
        else { return .system }
        let code = first.split(separator: "-").first.map(String.init) ?? first
        return AppLanguage(rawValue: code) ?? .system
    }

    /// Keeps the choice: an override for a language, none for the system.
    func store(in defaults: UserDefaults = .standard) {
        if self == .system {
            defaults.removeObject(forKey: Self.overrideKey)
        } else {
            defaults.set([rawValue], forKey: Self.overrideKey)
        }
    }

    /// The choice when the app started, which is the language this run is in. Read once, early.
    static let atLaunch: AppLanguage = stored()

    /// Whether the app has to start again for what is chosen now to apply.
    static func needsRelaunch(choice: AppLanguage) -> Bool { choice != atLaunch }
}

/// `--language <en|fr>`, which every command of the app takes to run in a language for that run
/// only, whatever the system and the Settings say. It is how the pictures of the interface are
/// made in each language.
enum LanguageLaunch {
    static let flag = "--language"
    static let usage = "[\(flag) <\(AppLanguage.shipped.joined(separator: "|"))>]"

    enum Outcome: Equatable {
        /// The flag is not there: carry on.
        case none
        /// Start again with these arguments, which carry the language as `-AppleLanguages`.
        case restart([String])
        /// The flag is there without a language we ship.
        case invalid
    }

    /// Takes the flag out of the arguments and puts `-AppleLanguages (xx)` in its place, right
    /// after the program, where the defaults read it and the command's own parsing never sees it.
    static func plan(_ arguments: [String]) -> Outcome {
        guard let index = arguments.firstIndex(of: flag) else { return .none }
        guard index + 1 < arguments.count, AppLanguage.shipped.contains(arguments[index + 1]) else { return .invalid }
        var rest = arguments
        rest.removeSubrange(index...(index + 1))
        let head = rest.prefix(1)
        return .restart(Array(head) + [AppLanguage.overrideKey.prefixedWithDash, "(\(arguments[index + 1]))"] + rest.dropFirst())
    }

    /// Replaces this process with the same program and the arguments of a plan. Foundation reads
    /// the language once, as the program starts, so a start again is how a run changes it. Only
    /// returns when it could not.
    static func restart(with arguments: [String]) {
        guard let path = Bundle.main.executablePath ?? arguments.first else { return }
        var pointers: [UnsafeMutablePointer<CChar>?] = arguments.map { strdup($0) }
        pointers.append(nil)
        execv(path, pointers)
    }
}

private extension String {
    var prefixedWithDash: String { "-" + self }
}

#if canImport(AppKit)
import AppKit

/// Starts the app again, for a change of language to apply.
enum AppRelaunch {
    /// Opens a second instance of the app, and quits this one once it is on its way.
    @MainActor
    static func now() {
        let configuration = NSWorkspace.OpenConfiguration()
        configuration.createsNewApplicationInstance = true
        NSWorkspace.shared.openApplication(at: Bundle.main.bundleURL, configuration: configuration) { _, error in
            guard error == nil else { return }
            DispatchQueue.main.async { NSApplication.shared.terminate(nil) }
        }
    }
}
#endif
