import Foundation

/// The search of the node palette: which node types match what was typed, best first.
///
/// A query matches a type when it is a prefix, a part, or a subsequence of the label or of the
/// name, such as `attrn` for "Attribute Noise". The tags give a smaller score of their own, so
/// "primitive" finds the cube, and add to the score of a type that matches by its label too.
enum NodeSearch {
    struct Result: Equatable {
        var type: EditorNodeType
        var score: Int
    }

    /// The types that match the query, best first. An empty query lists every type by label.
    static func rank(_ query: String, in types: [EditorNodeType]) -> [Result] {
        let needle = normalized(query)
        if needle.isEmpty {
            return types.sorted(by: alphabetical).map { Result(type: $0, score: 0) }
        }

        var results: [Result] = []
        for type in types {
            guard let score = score(needle, for: type) else { continue }
            results.append(Result(type: type, score: score))
        }
        return results.sorted { left, right in
            if left.score != right.score { return left.score > right.score }
            // Among equal scores the shorter label is the closer match.
            if left.type.label.count != right.type.label.count { return left.type.label.count < right.type.label.count }
            return alphabetical(left.type, right.type)
        }
    }

    /// The score of a type for a query that is already normalised, or nil when it does not match.
    static func score(_ needle: String, for type: EditorNodeType) -> Int? {
        let shortName = Self.shortName(of: type.name)
        let labelScore = ([type.label] + type.searchLabels)
            .compactMap { fieldScore(needle, in: normalized($0), words: words(of: $0)) }.max()
        let nameScore = fieldScore(needle, in: normalized(shortName), words: words(of: shortName))
        let tagScore = (type.tags + type.searchTags).compactMap { fieldScore(needle, in: normalized($0), words: []) }.max()

        let fieldBest = [labelScore, nameScore].compactMap { $0 }.max()
        switch (fieldBest, tagScore) {
        case let (field?, tag?): return field + tag / 4
        case let (field?, nil): return field
        // A tag alone matches, but below any match on the label or the name.
        case let (nil, tag?): return tag / 2
        case (nil, nil): return nil
        }
    }

    /// `helix::attributeNoise` shows as `attributeNoise`.
    static func shortName(of fullName: String) -> String {
        fullName.components(separatedBy: "::").last ?? fullName
    }

    private static func fieldScore(_ needle: String, in field: String, words: [String]) -> Int? {
        guard !field.isEmpty else { return nil }
        if field == needle { return 1000 }
        if field.hasPrefix(needle) { return 800 - min(field.count - needle.count, 100) }
        if words.dropFirst().contains(where: { $0.hasPrefix(needle) }) { return 600 }
        if let range = field.range(of: needle) {
            return 400 - min(field.distance(from: field.startIndex, to: range.lowerBound), 100)
        }
        return subsequenceScore(needle, in: field)
    }

    /// A score for letters of the query that come in order in the field, higher when they sit
    /// close together and near the start. Nil when they do not all come.
    private static func subsequenceScore(_ needle: String, in field: String) -> Int? {
        guard needle.count >= 2 else { return nil }
        var from = field.startIndex
        var first: String.Index?
        var previous: String.Index?
        var gaps = 0
        for letter in needle {
            guard let found = field[from...].firstIndex(of: letter) else { return nil }
            if first == nil { first = found }
            if let previous, field.index(after: previous) != found { gaps += 1 }
            previous = found
            from = field.index(after: found)
        }
        let start = first.map { field.distance(from: field.startIndex, to: $0) } ?? 0
        return max(200 - gaps * 25 - min(start, 50), 20)
    }

    /// Lower case, without accents, spaces, underscores and dots, so "Copy To" matches "copyTo"
    /// and "camera" finds "Caméra".
    private static func normalized(_ text: String) -> String {
        text.folding(options: .diacriticInsensitive, locale: nil).lowercased()
            .filter { !$0.isWhitespace && $0 != "_" && $0 != "." }
    }

    /// The words of a label or a name: "Attribute Noise" and "attributeNoise" give two.
    private static func words(of text: String) -> [String] {
        var words: [String] = []
        var current = ""
        for character in text.folding(options: .diacriticInsensitive, locale: nil) {
            if character.isWhitespace || character == "_" {
                if !current.isEmpty { words.append(current.lowercased()); current = "" }
            } else if character.isUppercase, current.last?.isLowercase == true {
                words.append(current.lowercased())
                current = String(character)
            } else {
                current.append(character)
            }
        }
        if !current.isEmpty { words.append(current.lowercased()) }
        return words
    }

    private static func alphabetical(_ left: EditorNodeType, _ right: EditorNodeType) -> Bool {
        left.label.localizedCaseInsensitiveCompare(right.label) == .orderedAscending
    }
}
