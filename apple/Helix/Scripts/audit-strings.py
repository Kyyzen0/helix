#!/usr/bin/env python3
"""Lists the string literals of the Swift app that may show text to a person without going
through localization.

  python3 -I apple/Helix/Scripts/audit-strings.py            print the report; exit 1 on a finding
  python3 -I apple/Helix/Scripts/audit-strings.py --keys     also list the keys the code looks up
  python3 -I apple/Helix/Scripts/audit-strings.py --stringsdata <DerivedData folder>
                                                             compare the catalog with the keys the
                                                             Swift compiler itself extracted
                                                             (build with SWIFT_EMIT_LOC_STRINGS=YES)

It reads every .swift file under apple/Helix/Sources (a small lexer: comments, strings with
interpolation, multi-line strings) and checks each literal against Resources/Localizable.xcstrings:

  missing-key      a literal looked up for display (Text, Button, Label, .help, String(localized:),
                   ...) that has no entry in the catalog, so it would show in English everywhere
  not-looked-up    a text of the catalog passed where nothing looks it up (a plain String argument)
  ternary-string   Text(flag ? "a" : "b"), which Swift types as String and never localizes
  suspicious       a literal that reads like a sentence or a label and is neither looked up nor
                   an identifier, a path, a flag or a symbol name

A literal followed by `// l10n-ok` (or on the line under such a comment) is accepted as it is: an
endonym such as "Français", or text that is meant to stay as written. Files that only write to
the command line or to an agent are skipped, and listed at the end: the MCP server and the
`--render-*` commands stay in English. Standard library only.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCES = ROOT / "apple" / "Helix" / "Sources"
CATALOG = ROOT / "apple" / "Helix" / "Resources" / "Localizable.xcstrings"

# Files whose text is for a terminal or an agent, not for a person at the window.
EXEMPT = {
    "MCPServer.swift": "the MCP server speaks to agents, in English",
    "MCPTools.swift": "the MCP server speaks to agents, in English",
    "MCPHandlers.swift": "the MCP server speaks to agents, in English",
    "MCPKeyframeHandlers.swift": "the MCP server speaks to agents, in English",
    "MCPJSON.swift": "the MCP server speaks to agents, in English",
    "main.swift": "command line entry",
    "EngineMessages.swift": "the English keys of the Errors table, which the engine's own messages are matched with",
    "OffscreenRender.swift": "command line pictures",
}
EXEMPT_SUFFIXES = {"SnapshotCommand.swift": "the --render-* commands write to the terminal, in English"}

# What takes a literal and looks it up in the catalog.
LOOKUP_CALLEES = {
    "Text", "Button", "Label", "Toggle", "Picker", "Menu", "Section", "CommandMenu", "DisclosureGroup",
    "TextField", "SecureField", "LocalizedStringKey", "LabeledContent", "Link", "Stepper", "Tab",
    "help", "accessibilityLabel", "accessibilityValue", "accessibilityHint", "navigationTitle",
    "alert", "confirmationDialog", "TabView", "Window",
}
# Helpers of ours that take the text of a control as a LocalizedStringKey, after other arguments.
LOOKUP_HELPERS = {"button", "labelled"}
SYMBOL = re.compile(r"^[a-z0-9]+(\.[a-z0-9]+)+$")
# Of those, the ones whose argument is a plain String when it is a ternary of literals.
TERNARY_CALLEES = {"Text", "Button", "Label", "Toggle", "Picker", "Menu", "Section", "DisclosureGroup", "LabeledContent"}
LOOKUP_LABELS = {"localized"}


class Literal:
    def __init__(self, line, text, before, raw, ok):
        self.line = line
        self.text = text          # interpolations are \x00
        self.before = before      # the code before the opening quote, comments removed
        self.raw = raw
        self.ok = ok


def lex(source):
    """The string literals of a Swift file, with the code before each."""
    literals = []
    code = []                     # code characters so far, comments and strings blanked
    i, n, line = 0, len(source), 1
    ok_lines = set()
    for number, text in enumerate(source.splitlines(), start=1):
        if "l10n-ok" in text:
            ok_lines.add(number)
            ok_lines.add(number + 1)

    def read_string(start, multiline):
        """Returns (text with interpolations as \\x00, index after the literal, lines used)."""
        j = start
        out = []
        newlines = 0
        while j < n:
            c = source[j]
            if multiline and source.startswith('"""', j):
                return "".join(out), j + 3, newlines
            if not multiline and c == '"':
                return "".join(out), j + 1, newlines
            if c == "\\" and j + 1 < n:
                if source[j + 1] == "(":
                    depth, j = 1, j + 2
                    while j < n and depth:
                        if source[j] == "(":
                            depth += 1
                        elif source[j] == ")":
                            depth -= 1
                        elif source[j] == '"':
                            _, j2, extra = read_string(j + 1, False)
                            newlines += extra
                            j = j2 - 1
                        j += 1
                    out.append("\x00")
                    continue
                if multiline and source[j + 1] == "\n":
                    out.append("\x01")   # a line continuation: dropped once the indentation is
                    j += 1
                    continue
                out.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(source[j + 1], source[j + 1]))
                j += 2
                continue
            if c == "\n":
                newlines += 1
                if not multiline:
                    return "".join(out), j, newlines
            out.append(c)
            j += 1
        return "".join(out), j, newlines

    while i < n:
        c = source[i]
        if source.startswith("//", i):
            while i < n and source[i] != "\n":
                i += 1
            continue
        if source.startswith("/*", i):
            depth, i = 1, i + 2
            while i < n and depth:
                if source.startswith("/*", i):
                    depth, i = depth + 1, i + 2
                elif source.startswith("*/", i):
                    depth, i = depth - 1, i + 2
                else:
                    if source[i] == "\n":
                        line += 1
                    i += 1
            continue
        if source.startswith('"""', i):
            body_start = source.index("\n", i) + 1
            text, end, newlines = read_string(body_start, True)
            literals.append(Literal(line, _dedent(text), "".join(code)[-240:], True, line in ok_lines))
            line += 1 + newlines
            code.append('""')
            i = end
            continue
        if c == '"':
            text, end, newlines = read_string(i + 1, False)
            literals.append(Literal(line, text, "".join(code)[-240:], False, line in ok_lines))
            line += newlines
            code.append('""')
            i = end
            continue
        if c == "\n":
            line += 1
        code.append(c)
        i += 1
    return literals


def _dedent(text):
    lines = text.split("\n")
    if lines and not lines[-1].strip():
        indent = len(lines[-1])
        lines = lines[:-1]
    else:
        indent = min((len(l) - len(l.lstrip()) for l in lines if l.strip()), default=0)
    joined = "\n".join(l[indent:] if len(l) >= indent else l.lstrip() for l in lines)
    # A backslash at the end of a line joins it to the next one.
    return joined.replace("\x01\n", "")


def callee_of(before):
    """How the literal that follows `before` is used: ("call", name) for `name(` right before it,
    ("label", name) for `name:`, ("ternary", None) after `?` or `:` of a conditional, or None."""
    tail = before.rstrip()
    if tail.endswith("("):
        match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\($", tail)
        return ("call", match.group(1)) if match else ("call", "")
    match = re.search(r"([A-Za-z_][A-Za-z0-9_]*):$", tail)
    if match:
        return ("label", match.group(1))
    if tail.endswith("?"):
        return ("ternary", None)
    if tail.endswith(":"):
        return ("ternary", None)
    if tail.endswith(","):
        return ("argument", None)
    return None


def enclosing_call(before):
    """The name of the call whose argument list the end of `before` is in."""
    depth = 0
    for index in range(len(before) - 1, -1, -1):
        char = before[index]
        if char == ")":
            depth += 1
        elif char == "(":
            if depth == 0:
                match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)$", before[:index])
                return match.group(1) if match else ""
            depth -= 1
    return ""


def normal(key):
    """A key with every format specifier or interpolation as one placeholder."""
    return re.sub(r"%(?:\d+\$)?(?:ll|l|h)?[@dfsuxXgeEc]|\x00", "\x00", key)


def load_keys():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))["strings"]
    return {normal(key): key for key in catalog}


IDENTIFIER = re.compile(r"^[A-Za-z0-9_.:#/\-]+$")


def looks_like_text(text):
    stripped = text.replace("\x00", "")
    letters = re.findall(r"[A-Za-zÀ-ÿ]{3,}", stripped)
    if not letters:
        return False
    if IDENTIFIER.match(stripped) and " " not in stripped:
        # A single word: text when it is capitalised ("Position"), an identifier otherwise.
        return bool(re.match(r"^[A-Z][a-z]{2,}$", stripped))
    if stripped.startswith(("--", "/", ".", "%", "http", "usage:", "#")) or "::" in stripped:
        return False
    return " " in stripped or bool(re.match(r"^[A-Z]", stripped))


def audit():
    keys = load_keys()
    used = {}
    findings = []
    skipped = {}
    for path in sorted(SOURCES.rglob("*.swift")):
        name = path.name
        reason = EXEMPT.get(name) or next((r for s, r in EXEMPT_SUFFIXES.items() if name.endswith(s)), None)
        if reason:
            skipped[name] = reason
            continue
        relative = path.relative_to(SOURCES)
        for literal in lex(path.read_text(encoding="utf-8")):
            if literal.ok:
                continue
            how = callee_of(literal.before)
            enclosing = enclosing_call(literal.before)
            looked_up = bool(how) and (
                (how[0] == "call" and how[1] in LOOKUP_CALLEES)
                or (how[0] == "label" and how[1] in LOOKUP_LABELS)
                or (enclosing in LOOKUP_HELPERS and not SYMBOL.match(literal.text))
            )
            if enclosing in LOOKUP_HELPERS and SYMBOL.match(literal.text):
                continue
            if how and how[0] == "call" and how[1] == "Text" and literal.before.rstrip().endswith("verbatim:"):
                continue
            if literal.before.rstrip().endswith("verbatim:"):
                continue
            where = f"{relative}:{literal.line}"
            normalised = normal(literal.text)
            if how and how[0] == "ternary" and enclosing in TERNARY_CALLEES and looks_like_text(literal.text):
                findings.append(("ternary-string", where, literal.text))
                continue
            if looked_up:
                if not literal.text.strip() or not re.search(r"[A-Za-zÀ-ÿ]", literal.text):
                    continue
                used.setdefault(normalised, []).append(where)
                if normalised not in keys:
                    findings.append(("missing-key", where, literal.text))
            elif looks_like_text(literal.text):
                if normalised in keys:
                    findings.append(("not-looked-up", where, literal.text))
                else:
                    findings.append(("suspicious", where, literal.text))
    return keys, used, findings, skipped


def compiler_keys(folder):
    """The keys of the Localizable table that the compiler extracted, from its .stringsdata files."""
    keys = set()
    for path in Path(folder).rglob("*.stringsdata"):
        data = json.loads(path.read_text(encoding="utf-8"))
        for entry in data.get("tables", {}).get("Localizable", []):
            if re.search(r"[A-Za-z]{2,}", entry["key"]):
                keys.add(entry["key"])
    return keys


def compare_with_compiler(folder):
    """Reports the texts the compiler looks up that the catalog lacks, and the other way round."""
    catalog = set(json.loads(CATALOG.read_text(encoding="utf-8"))["strings"])
    extracted = compiler_keys(folder)
    missing = sorted(extracted - catalog)
    stale = sorted(key for key in catalog - extracted if re.search(r"[A-Za-z]{2,}", key))
    print(f"compiler: {len(extracted)} keys; catalog: {len(catalog)} keys")
    for key in missing:
        print("  missing-from-catalog", repr(key))
    for key in stale:
        print("  not-extracted       ", repr(key))
    return 1 if missing else 0


def main(arguments):
    if "--stringsdata" in arguments:
        return compare_with_compiler(arguments[arguments.index("--stringsdata") + 1])
    show_keys = "--keys" in arguments
    keys, used, findings, skipped = audit()
    for kind, where, text in findings:
        print(f"{kind:15} {where}  {text.replace(chr(0), '%@')!r}")
    unused = sorted(
        original for normalised, original in keys.items()
        if normalised not in used and re.search(r"[A-Za-z]{2,}", normalised)
    )
    print()
    print(f"{len(findings)} findings; {len(used)} keys looked up in the code; {len(keys)} keys in the catalog")
    if show_keys:
        for normalised in sorted(used):
            print("  key:", normalised.replace("\x00", "%@")[:100].replace("\n", "\\n"))
    if unused:
        print(f"{len(unused)} catalog keys the scan does not see looked up (used another way, or stale):")
        for key in unused:
            print("  -", key.replace("\n", "\\n")[:100])
    print("skipped files:")
    for name, reason in skipped.items():
        print(f"  {name}: {reason}")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
