import Foundation

/// The names of nodes: the one a new node gets from the label of its type, and what a person may
/// type to rename one.
///
/// The engine names a node it makes with the internal name of its type ("sineWave1"), which is
/// English whatever the language of the app. A node made from the editor is named from the label
/// the person reads instead ("ondeSinusoidale1" in French), so the name a card shows is a word
/// of their language. The engine numbers it from there (`freeNodeName`).
enum NodeNaming {
    /// The longest name the engine takes (`maxNodeNameLength` in the facade).
    static let maxLength = 64

    /// Words a name leaves out: articles and particles, which say nothing about the node.
    private static let smallWords: Set<String> = ["de", "du", "des", "la", "le", "les", "of", "the"]

    /// The letters and digits of a word, with the marks taken off and the ligatures spelled out.
    private static func plain(_ word: String) -> String {
        let spelled = word
            .replacingOccurrences(of: "œ", with: "oe").replacingOccurrences(of: "Œ", with: "Oe")
            .replacingOccurrences(of: "æ", with: "ae").replacingOccurrences(of: "Æ", with: "Ae")
            .replacingOccurrences(of: "ß", with: "ss")
        let folded = spelled.folding(options: [.diacriticInsensitive, .widthInsensitive], locale: Locale(identifier: "en_US"))
        return String(folded.unicodeScalars.filter { $0.isASCII && (CharacterSet.alphanumerics.contains($0)) })
    }

    /// Whether the engine accepts a text as a name: letters, digits and underscores, and not
    /// longer than `maxLength`.
    static func isValid(_ name: String) -> Bool {
        !name.isEmpty && name.utf8.count <= maxLength && name.unicodeScalars.allSatisfy {
            $0.isASCII && (CharacterSet.alphanumerics.contains($0) || $0 == "_")
        }
    }

    /// The name, without its number, a node of a type with this label is given: the words of the
    /// label in lowerCamelCase, without accents, the elided particles ("d'", "l'") and the small
    /// words ("de", "the") left out. "Onde sinusoïdale" gives "ondeSinusoidale", "Bruit d'attribut"
    /// gives "bruitAttribut" and "Sine Wave" gives "sineWave". Empty when nothing usable is left,
    /// and then the engine's own name is the one used.
    static func baseName(forLabel label: String) -> String {
        var words: [String] = []
        for token in label.split(whereSeparator: { $0.isWhitespace || $0 == "-" || $0 == "/" }) {
            var word = String(token)
            // "d'attribut" is the particle and a word: only the word is kept.
            if let apostrophe = word.firstIndex(where: { $0 == "'" || $0 == "’" || $0 == "ʼ" }) {
                word = String(word[word.index(after: apostrophe)...])
            }
            let letters = plain(word)
            guard !letters.isEmpty, !smallWords.contains(letters.lowercased()) else { continue }
            words.append(letters)
        }
        guard let first = words.first else { return "" }
        let rest = words.dropFirst().map { $0.prefix(1).uppercased() + $0.dropFirst().lowercased() }
        let name = ([first.lowercased()] + rest).joined()
        // Room is left for the number the engine puts after it.
        return isValid(name) && name.utf8.count <= maxLength - 4 ? name : ""
    }

    /// The name a new node is asked for, before the engine numbers it: the base name and a 1, or
    /// empty to let the engine name the node.
    static func firstName(forLabel label: String) -> String {
        let base = baseName(forLabel: label)
        return base.isEmpty ? "" : base + "1"
    }

    /// What is wrong with a text typed as a name, in words for a person, or nil when it is fine.
    /// The engine says the rest (a name another node has).
    static func problem(with name: String) -> String? {
        if name.isEmpty { return String(localized: "A name cannot be empty.") }
        if name.utf8.count > maxLength { return String(localized: "A name has at most \(maxLength) characters.") }
        if !isValid(name) {
            return String(localized: "Use letters, digits and underscores, with no spaces or accents.")
        }
        return nil
    }
}
