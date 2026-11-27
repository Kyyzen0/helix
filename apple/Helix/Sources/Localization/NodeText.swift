import Foundation

/// The words the engine gives a node in English, in the language of the app.
///
/// The labels, tooltips and dropdown entries of a node come from its `node.yaml`, in English.
/// The "Nodes" string table holds their translations under stable keys (see
/// `Scripts/extract-node-strings.py`), and every lookup falls back to the engine's own text, so
/// a node that is not translated yet, or one a script defines, still reads in English.
enum NodeText {
    static let table = "Nodes" // l10n-ok: the name of the string table

    /// The translation of a key, or nil when the table has none for it.
    static func translation(of key: String, in bundle: Bundle = .main) -> String? {
        let text = bundle.localizedString(forKey: key, value: nil, table: table)
        return text == key || text.isEmpty ? nil : text
    }

    private static func localized(_ key: String, fallback: String, in bundle: Bundle) -> String {
        translation(of: key, in: bundle) ?? fallback
    }

    /// The name of a node type, such as "Cube" for `helix::cube`.
    static func typeLabel(_ type: String, fallback: String, in bundle: Bundle = .main) -> String {
        localized("node.\(type).label", fallback: fallback, in: bundle)
    }

    /// The label of an input port.
    static func inputLabel(_ type: String, index: Int, fallback: String, in bundle: Bundle = .main) -> String {
        localized("node.\(type).input.\(index)", fallback: fallback, in: bundle)
    }

    /// A search tag, such as "curve".
    static func tag(_ tag: String, in bundle: Bundle = .main) -> String {
        localized("tag.\(tag)", fallback: tag, in: bundle)
    }

    /// The label of a parameter, a group or a divider.
    static func label(_ type: String, parameter: String, fallback: String, in bundle: Bundle = .main) -> String {
        localized("param.\(type).\(parameter).label", fallback: fallback, in: bundle)
    }

    static func tooltip(_ type: String, parameter: String, fallback: String, in bundle: Bundle = .main) -> String {
        localized("param.\(type).\(parameter).tooltip", fallback: fallback, in: bundle)
    }

    /// The caption a range circle shows beside its numbers.
    static func caption(_ type: String, parameter: String, fallback: String, in bundle: Bundle = .main) -> String {
        localized("param.\(type).\(parameter).caption", fallback: fallback, in: bundle)
    }

    /// An entry of a dropdown.
    static func option(_ type: String, parameter: String, value: String, fallback: String, in bundle: Bundle = .main) -> String {
        localized("option.\(type).\(parameter).\(value)", fallback: fallback, in: bundle)
    }
}

/// The interpolations of a ramp, which the engine names in English for every node.
enum RampText {
    static func interpolation(_ value: String, fallback: String) -> String {
        switch value {
        case "constant": String(localized: "Constant")
        case "linear": String(localized: "Linear")
        case "bspline": String(localized: "B Spline")
        default: fallback
        }
    }
}
