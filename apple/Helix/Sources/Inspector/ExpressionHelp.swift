import Foundation

/// What a person can write in an expression: the variables and the functions, each with one
/// short example. The examples are exactly what the engine compiles (a test in
/// `tests/ExpressionEngineTests.cpp` evaluates them), so clicking one is never an error.
struct ExpressionHelpItem: Identifiable, Equatable {
    /// What is written, such as `$F`.
    let syntax: String
    /// What it gives, in a sentence.
    let summary: String
    /// An example a click puts in the field.
    let example: String

    var id: String { syntax }
}

enum ExpressionHelp {
    static var items: [ExpressionHelpItem] {
        [
            ExpressionHelpItem(
                syntax: "$F",
                summary: String(localized: "The current frame."),
                example: "$F * 0.1"
            ),
            ExpressionHelpItem(
                syntax: "$T",
                summary: String(localized: "The time in seconds."),
                // l10n-ok: code the engine reads
                example: "sin($T * 2)"
            ),
            ExpressionHelpItem(
                syntax: "prm(\"node.name\")",
                summary: String(localized: "The value of another setting: the name of its node, a dot, then its name."),
                example: "prm(\"grille1.size\")"
            ),
            ExpressionHelpItem(
                syntax: "rand(seed)",
                summary: String(localized: "A number from 0 to 1, always the same for the same seed."),
                example: "rand($F)"
            ),
            ExpressionHelpItem(
                // l10n-ok: the names of the functions
                syntax: "sin  cos  abs  min  max",
                summary: String(localized: "The usual maths. Write decimals with a point, as in 10.0."),
                // l10n-ok: code the engine reads
                example: "max($F - 10.0, 0.0)"
            ),
        ]
    }
}
