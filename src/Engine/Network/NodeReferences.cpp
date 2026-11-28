#include "Engine/Network/NodeReferences.h"
#include "Engine/Network/Network.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Parameter/Parameter.h"
#include "Engine/Serializer/ParameterSerializable.h"
#include <cctype>
#include <string_view>

namespace helix::nt {

namespace {

bool startsIdentifier(char character)
{
    return std::isalpha(static_cast<unsigned char>(character)) || character == '_';
}

bool continuesIdentifier(char character)
{
    return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
}

bool readsParameter(std::string_view identifier)
{
    return identifier == "prm" || identifier == "prmI" || identifier == "prmS";
}

// Rewrites what sits between the quotes of a prm call: "grid1.translate", "/grid1.translate",
// "../grid1.translate". The node part is resolved like the engine does, and only a reference
// that lands on a renamed node changes.
std::string rewriteReference(const std::string& literal, const Path& scope, const NodeRenames& renames)
{
    // The engine strips the whitespace around a reference before it reads it.
    const std::size_t first = literal.find_first_not_of(" \t");
    if (first == std::string::npos) return literal;
    const std::size_t last = literal.find_last_not_of(" \t");
    const std::string lead = literal.substr(0, first);
    const std::string trail = literal.substr(last + 1);
    const std::string core = literal.substr(first, last - first + 1);

    const std::size_t slash = core.rfind('/');
    const std::size_t leafStart = slash == std::string::npos ? 0 : slash + 1;
    const std::string leaf = core.substr(leafStart);
    // A final ".." steps up, and a reference with no node part reads the node itself, which
    // follows it wherever it goes.
    if (leaf == "..") return literal;
    const std::size_t dot = leaf.find('.');
    const std::string nodeLeaf = leaf.substr(0, dot);
    if (nodeLeaf.empty()) return literal;

    const std::string nodePart = core.substr(0, leafStart + nodeLeaf.size());
    if (!Path::isValidFormatting(nodePart)) return literal;

    const std::string absolute = Path(nodePart).makeAbsoluteFrom(scope).getString();
    const auto renamed = renames.find(absolute);
    if (renamed == renames.end()) return literal;

    const std::string newLeaf = Path(renamed->second).getName();
    return lead + core.substr(0, leafStart) + newLeaf +
           (dot == std::string::npos ? std::string() : leaf.substr(dot)) + trail;
}

} // namespace

std::string rewriteNodeReferences(const std::string& text, const Path& scope, const NodeRenames& renames)
{
    if (renames.empty() || text.find("prm") == std::string::npos) return text;

    enum class Expecting
    {
        Nothing,
        Parenthesis,
        String
    };

    std::string result;
    result.reserve(text.size());
    Expecting expecting = Expecting::Nothing;
    std::size_t index = 0;
    const std::size_t size = text.size();

    while (index < size)
    {
        const char character = text[index];

        // Comments hold no reference, and a quote inside one starts nothing.
        if (character == '/' && index + 1 < size && text[index + 1] == '/')
        {
            const std::size_t end = text.find('\n', index);
            const std::size_t stop = end == std::string::npos ? size : end;
            result.append(text, index, stop - index);
            index = stop;
            expecting = Expecting::Nothing;
            continue;
        }
        if (character == '/' && index + 1 < size && text[index + 1] == '*')
        {
            const std::size_t end = text.find("*/", index + 2);
            const std::size_t stop = end == std::string::npos ? size : end + 2;
            result.append(text, index, stop - index);
            index = stop;
            expecting = Expecting::Nothing;
            continue;
        }

        if (character == '"')
        {
            std::size_t end = index + 1;
            while (end < size && text[end] != '"')
                end += text[end] == '\\' ? 2 : 1;
            if (end >= size)
            {
                // A string that never closes is left as it is.
                result.append(text, index, std::string::npos);
                break;
            }
            const std::string literal = text.substr(index + 1, end - index - 1);
            result += '"';
            // A string with an escape in it is not a plain path, so it is not read as one.
            if (expecting == Expecting::String && literal.find('\\') == std::string::npos)
                result += rewriteReference(literal, scope, renames);
            else
                result += literal;
            result += '"';
            index = end + 1;
            expecting = Expecting::Nothing;
            continue;
        }

        if (startsIdentifier(character))
        {
            std::size_t end = index;
            while (end < size && continuesIdentifier(text[end])) ++end;
            const std::string_view identifier(text.data() + index, end - index);
            // A member or an attribute that happens to be called prm is not the function.
            const bool member = index > 0 && (text[index - 1] == '.' || text[index - 1] == '@');
            expecting = (!member && readsParameter(identifier)) ? Expecting::Parenthesis : Expecting::Nothing;
            result.append(identifier);
            index = end;
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(character)))
        {
            result += character;
            ++index;
            continue;
        }

        expecting = (character == '(' && expecting == Expecting::Parenthesis) ? Expecting::String
                                                                                : Expecting::Nothing;
        result += character;
        ++index;
    }
    return result;
}

bool rewriteNodeReferences(prm::Parameter& parameter, const Path& scope, const NodeRenames& renames)
{
    bool changed = false;

    if (parameter.getTemplate().isMultiParm())
    {
        for (unsigned int instance = 0; instance < parameter.getInstanceCount(); ++instance)
            for (const auto& field : parameter.getInstance(instance))
                changed |= rewriteNodeReferences(*field, scope, renames);
        return changed;
    }

    for (unsigned int component = 0; component < parameter.getVectorSize(); ++component)
    {
        if (const std::optional<String> expression = parameter.getExpression(component))
        {
            const std::string rewritten = rewriteNodeReferences(*expression, scope, renames);
            if (rewritten == *expression) continue;
            parameter.setExpression(rewritten, component);
            changed = true;
        }
        else if (parameter.getValueType() == prm::ValueType::String)
        {
            // A script is a string value, and reads parameters the way an expression does.
            const String value = parameter.evalString(component);
            const std::string rewritten = rewriteNodeReferences(value, scope, renames);
            if (rewritten == value) continue;
            parameter.setString(rewritten, component);
            changed = true;
        }
    }
    return changed;
}

bool rewriteNodeReferences(ParameterSerializable& parameter, const Path& scope, const NodeRenames& renames)
{
    bool changed = false;

    for (auto& expression : parameter.expressions)
    {
        if (!expression) continue;
        const std::string rewritten = rewriteNodeReferences(*expression, scope, renames);
        if (rewritten == *expression) continue;
        expression = rewritten;
        changed = true;
    }

    // A component driven by an expression stores the value the expression gave, which is
    // computed again when the expression is applied, so only the literals are read.
    for (std::size_t component = 0; component < parameter.stringValues.size(); ++component)
    {
        const bool driven = component < parameter.expressions.size() && parameter.expressions[component];
        if (driven) continue;
        const std::string rewritten = rewriteNodeReferences(parameter.stringValues[component], scope, renames);
        if (rewritten == parameter.stringValues[component]) continue;
        parameter.stringValues[component] = rewritten;
        changed = true;
    }

    for (auto& instance : parameter.instances)
        for (ParameterSerializable& field : instance)
            changed |= rewriteNodeReferences(field, scope, renames);
    return changed;
}

bool rewriteNodeReferences(Network& network, const NodeRenames& renames)
{
    bool changed = false;
    for (auto [nodeId, node] : network.nodes())
    {
        const Path scope = node.getPath().getParent();
        for (const auto& weakParameter : node.getParameters())
            if (auto parameter = weakParameter.lock())
                changed |= rewriteNodeReferences(*parameter, scope, renames);
    }
    return changed;
}

} // namespace helix::nt
