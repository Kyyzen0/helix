#!/usr/bin/env python3
"""Keeps Resources/Nodes.xcstrings in step with the node.yaml files.

The engine gives the app English labels, tooltips and dropdown options. The app looks each one
up in the "Nodes" string table by a stable key and falls back to the engine's text when the key
is missing, so a new node still shows up in English until it is translated.

  python3 -I apple/Helix/Scripts/extract-node-strings.py            write the catalog
  python3 -I apple/Helix/Scripts/extract-node-strings.py --check    change nothing; exit 1 when the
                                                                    catalog is out of date or a
                                                                    key has no French

Keys (the type is `<namespace>::<name>`, as the engine names it):
  node.<type>.label                 the name of the node type
  node.<type>.description           the sentence that says what the node does (palette, menus)
  node.<type>.input.<index>         the label of an input port
  param.<type>.<name>.label         a parameter, a group or a divider
  param.<type>.<name>.tooltip       its tooltip
  param.<type>.<name>.caption       the caption a range circle shows beside its numbers
  option.<type>.<param>.<value>     an entry of a dropdown
  tag.<tag>                         a search tag, shared by every node

Existing French translations are kept. A key whose English changed keeps its French but is
marked "needs_review"; a key that no longer exists is dropped. Standard library only.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
NODES = ROOT / "nodes"
CATALOG = ROOT / "apple" / "Helix" / "Resources" / "Nodes.xcstrings"


# MARK: A reader for the subset of YAML the node files use

class YamlError(Exception):
    pass


def _strip_comment(line):
    quote = None
    for index, char in enumerate(line):
        if quote:
            if char == quote:
                quote = None
        elif char in "\"'":
            quote = char
        elif char == "#" and (index == 0 or line[index - 1] in " \t"):
            return line[:index]
    return line


def _scalar(text):
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in "\"'":
        inner = text[1:-1]
        return inner.replace('\\"', '"') if text[0] == '"' else inner.replace("''", "'")
    if text.startswith("{") or text.startswith("["):
        return _flow(text)
    if text in ("true", "True"):
        return True
    if text in ("false", "False"):
        return False
    if re.fullmatch(r"-?\d+", text):
        return int(text)
    if re.fullmatch(r"-?\d*\.\d+(e-?\d+)?", text):
        return float(text)
    return text


def _split_flow(text):
    parts, depth, quote, current = [], 0, None, ""
    for char in text:
        if quote:
            current += char
            if char == quote:
                quote = None
        elif char in "\"'":
            quote = char
            current += char
        elif char in "{[":
            depth += 1
            current += char
        elif char in "}]":
            depth -= 1
            current += char
        elif char == "," and depth == 0:
            parts.append(current)
            current = ""
        else:
            current += char
    if current.strip():
        parts.append(current)
    return parts


def _flow(text):
    text = text.strip()
    inner = text[1:-1]
    if text.startswith("["):
        return [_scalar(part) for part in _split_flow(inner)]
    result = {}
    for part in _split_flow(inner):
        key, _, value = part.partition(":")
        result[key.strip()] = _scalar(value)
    return result


def parse_yaml(text):
    lines = []
    for raw in text.splitlines():
        lines.append(raw.rstrip())
    position = 0

    def indent_of(line):
        return len(line) - len(line.lstrip(" "))

    def significant(index):
        while index < len(lines):
            stripped = _strip_comment(lines[index]).strip()
            if stripped:
                return index
            index += 1
        return None

    def block_scalar(index, parent_indent, folded):
        collected = []
        while index < len(lines):
            line = lines[index]
            if line.strip() and indent_of(line) <= parent_indent:
                break
            collected.append(line.strip())
            index += 1
        while collected and not collected[-1]:
            collected.pop()
        return (" " if folded else "\n").join(collected), index

    def parse_block(index, indent):
        index = significant(index)
        if index is None:
            return None, len(lines)
        line = _strip_comment(lines[index])
        if line.lstrip().startswith("- ") or line.strip() == "-":
            return parse_sequence(index, indent_of(line))
        return parse_mapping(index, indent_of(line))

    def parse_value(index, rest, indent):
        """The value after `key:` or `- `, which may continue on the next lines."""
        rest = rest.strip()
        if rest in (">", ">-", "|", "|-", ">+", "|+"):
            return block_scalar(index + 1, indent, rest.startswith(">"))
        if rest:
            return _scalar(rest), index + 1
        following = significant(index + 1)
        if following is not None and (
            indent_of(lines[following]) > indent
            or (indent_of(lines[following]) == indent and lines[following].lstrip().startswith("- "))
        ):
            return parse_block(index + 1, indent)
        return None, index + 1

    def parse_mapping(index, indent):
        result = {}
        while True:
            index = significant(index)
            if index is None:
                index = len(lines)
                break
            line = _strip_comment(lines[index])
            if indent_of(line) != indent or line.lstrip().startswith("- "):
                break
            key, colon, rest = line.strip().partition(":")
            if not colon:
                raise YamlError(f"line {index + 1}: expected a key")
            result[key.strip()], index = parse_value(index, rest, indent)
        return result, index

    def parse_sequence(index, indent):
        result = []
        while True:
            index = significant(index)
            if index is None:
                index = len(lines)
                break
            line = _strip_comment(lines[index])
            if indent_of(line) != indent or not line.lstrip().startswith("-"):
                break
            body = line.strip()[1:].strip()
            if re.match(r"^[A-Za-z_][\w]*\s*:(\s|$)", body):
                # A mapping that starts on the dash line: the dash counts as two columns.
                inner_indent = indent + 2
                lines[index] = " " * inner_indent + body
                item, index = parse_mapping(index, inner_indent)
            else:
                item, index = parse_value(index, body, indent)
            result.append(item)
        return result, index

    value, _ = parse_block(0, 0)
    return value


# MARK: The keys

def collect(node_dir):
    """Every translatable string of one node.yaml, as (key, English) in file order."""
    data = parse_yaml((node_dir / "node.yaml").read_text(encoding="utf-8"))
    type_name = f"{data['namespace']}::{data['name']}"
    out = []
    if data.get("label"):
        out.append((f"node.{type_name}.label", str(data["label"])))
    if data.get("description"):
        out.append((f"node.{type_name}.description", str(data["description"])))
    for index, port in enumerate(data.get("inputs") or []):
        if isinstance(port, dict) and port.get("label"):
            out.append((f"node.{type_name}.input.{index}", str(port["label"])))
    for tag in data.get("tags") or []:
        out.append((f"tag.{tag}", str(tag)))

    def walk(parameters):
        for parameter in parameters or []:
            name = parameter["name"]
            if parameter.get("label"):
                out.append((f"param.{type_name}.{name}.label", str(parameter["label"])))
            if parameter.get("tooltip"):
                out.append((f"param.{type_name}.{name}.tooltip", str(parameter["tooltip"])))
            caption = (parameter.get("styleOptions") or {}).get("caption")
            if caption:
                out.append((f"param.{type_name}.{name}.caption", str(caption)))
            for option in parameter.get("options") or []:
                if isinstance(option, dict) and option.get("label") is not None:
                    out.append((f"option.{type_name}.{name}.{option['value']}", str(option["label"])))
            walk(parameter.get("parameters"))

    walk(data.get("parameters"))
    return out


def collect_all():
    strings = {}
    for node_dir in sorted(NODES.iterdir()):
        if (node_dir / "node.yaml").is_file():
            for key, english in collect(node_dir):
                strings.setdefault(key, english)
    return strings


# MARK: The catalog

def load_catalog():
    if not CATALOG.is_file():
        return {}
    return json.loads(CATALOG.read_text(encoding="utf-8")).get("strings", {})


def french_of(entry):
    unit = entry.get("localizations", {}).get("fr", {}).get("stringUnit")
    return unit


def english_of(entry):
    return entry.get("localizations", {}).get("en", {}).get("stringUnit", {}).get("value")


def build(strings, existing):
    catalog = {}
    changed, missing = [], []
    for key, english in strings.items():
        localizations = {"en": {"stringUnit": {"state": "translated", "value": english}}}
        old = existing.get(key)
        french = french_of(old) if old else None
        if french and french.get("value"):
            state = french.get("state", "translated")
            if english_of(old) != english:
                state = "needs_review"
                changed.append(key)
            localizations["fr"] = {"stringUnit": {"state": state, "value": french["value"]}}
        else:
            missing.append(key)
        catalog[key] = {"extractionState": "manual", "localizations": localizations}
    return catalog, changed, missing


def render(catalog):
    document = {"sourceLanguage": "en", "strings": catalog, "version": "1.0"}
    return json.dumps(document, ensure_ascii=False, indent=2, sort_keys=False) + "\n"


def main(arguments):
    check = "--check" in arguments
    unknown = [a for a in arguments if a != "--check"]
    if unknown:
        print(__doc__)
        return 2
    strings = collect_all()
    existing = load_catalog()
    catalog, changed, missing = build(strings, existing)
    stale = [key for key in existing if key not in strings]
    text = render(catalog)
    current = CATALOG.read_text(encoding="utf-8") if CATALOG.is_file() else ""

    print(f"{len(strings)} strings in {sum(1 for _ in NODES.glob('*/node.yaml'))} nodes")
    if stale:
        print(f"{len(stale)} keys no longer in the node files")
    if changed:
        print(f"{len(changed)} keys whose English changed (French kept, marked needs_review)")
    if missing:
        print(f"{len(missing)} keys without French:")
        for key in missing[:40]:
            print(f"  {key}: {strings[key]!r}")
        if len(missing) > 40:
            print(f"  ... and {len(missing) - 40} more")

    if check:
        problems = bool(stale or changed or missing or current != text)
        if current != text and not (stale or changed or missing):
            print("the catalog does not match the node files; run the script without --check")
        return 1 if problems else 0
    if current != text:
        CATALOG.write_text(text, encoding="utf-8")
        print(f"wrote {CATALOG.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
