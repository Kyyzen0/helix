import Foundation

/// The text a person pastes into their AI app to connect it to Helix: the command that starts
/// `Helix --mcp`, in the form each app reads. Pure, so the forms are checked by tests.
enum AgentSetup {
    /// The apps Settings has a button for. The names are brands: they are not translated.
    enum Client: String, CaseIterable, Identifiable {
        case claudeCode
        case claudeDesktop
        case codex
        case gemini
        case generic

        var id: String { rawValue }

        var title: String {
            switch self {
            case .claudeCode: "Claude Code"
            case .claudeDesktop: "Claude Desktop"
            case .codex: "Codex CLI"
            case .gemini: "Gemini CLI"
            case .generic: "JSON"
            }
        }
    }

    /// What the app starts. The relay is `--mcp` alone: it edits the open Helix, and serves
    /// headless when none is open.
    static let arguments = ["--mcp"]
    static let serverName = "helix"

    /// The executable of the running app, which is what an AI app is told to start.
    static var binaryPath: String {
        Bundle.main.executablePath ?? CommandLine.arguments[0]
    }

    static func snippet(for client: Client, binary: String) -> String {
        switch client {
        case .claudeCode:
            return (["claude mcp add \(serverName) --", shellQuoted(binary)] + arguments).joined(separator: " ")
        case .claudeDesktop, .gemini, .generic:
            return mcpServersJSON(binary: binary)
        case .codex:
            let list = arguments.map { tomlString($0) }.joined(separator: ", ")
            return "[mcp_servers.\(serverName)]\ncommand = \(tomlString(binary))\nargs = [\(list)]\n"
        }
    }

    /// `{"mcpServers": {"helix": {"command": …, "args": ["--mcp"]}}}`, which Claude Desktop and
    /// Gemini CLI read in their own files, and many other apps.
    static func mcpServersJSON(binary: String) -> String {
        let object: [String: Any] = [
            "mcpServers": [serverName: ["command": binary, "args": arguments]],
        ]
        let data = try? JSONSerialization.data(
            withJSONObject: object, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
        )
        return data.flatMap { String(data: $0, encoding: .utf8) } ?? ""
    }

    /// A word for a shell: as it is when it is plain, and in single quotes when it is not.
    static func shellQuoted(_ word: String) -> String {
        let plain = CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-+:=@%")
        if !word.isEmpty, word.unicodeScalars.allSatisfy({ plain.contains($0) }) { return word }
        return "'" + word.replacingOccurrences(of: "'", with: "'\\''") + "'"
    }

    /// A basic string of TOML.
    static func tomlString(_ text: String) -> String {
        var out = "\""
        for scalar in text.unicodeScalars {
            switch scalar {
            case "\\": out += "\\\\"
            case "\"": out += "\\\""
            case "\n": out += "\\n"
            case "\t": out += "\\t"
            default:
                if scalar.value < 0x20 || scalar.value == 0x7F {
                    out += String(format: "\\u%04X", scalar.value)
                } else {
                    out.unicodeScalars.append(scalar)
                }
            }
        }
        return out + "\""
    }
}
