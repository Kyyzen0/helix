#pragma once
#include "Engine/Core/Path.h"
#include <string>
#include <unordered_map>

struct ParameterSerializable;

namespace helix::prm {
class Parameter;
}

namespace helix::nt {
class Network;

/// @brief The nodes whose name changes, from the absolute path a node had to the absolute path
/// it takes. The two paths of a pair share their parent: only the leaf changes.
using NodeRenames = std::unordered_map<std::string, std::string>;

/**
 * @brief Points the node references in a piece of expression or script text at the new names.
 *
 * A node is read by name through prm("grid1.translate"), prmI and prmS, the only places an
 * expression or a script holds a node path. Only the string given to one of those calls is
 * looked at, and the reference is resolved the way the engine resolves it (from the scope the
 * reader sits in, with "/", ".." and bare names), so a reference to "grid10" is not touched
 * when "grid1" is renamed, and a string that merely looks like a name is left alone. The shape
 * of the reference ("grid1.tx", "/grid1.tx", "../grid1.tx") is kept.
 *
 * @param text The expression or script.
 * @param scope The absolute path of the scope the node reading the text lives in.
 * @param renames The nodes that change name.
 * @return The text, with every reference to a renamed node pointing at its new name.
 */
std::string rewriteNodeReferences(const std::string& text, const Path& scope, const NodeRenames& renames);

/// @brief Rewrites the expressions and the string values of a live parameter (the fields of a
/// multi parameter included), see rewriteNodeReferences.
/// @return Whether anything changed.
bool rewriteNodeReferences(prm::Parameter& parameter, const Path& scope, const NodeRenames& renames);

/// @brief Rewrites the expressions and the string values of a captured parameter.
/// @return Whether anything changed.
bool rewriteNodeReferences(ParameterSerializable& parameter, const Path& scope, const NodeRenames& renames);

/// @brief Rewrites the references of every parameter of every node of the network.
/// @return Whether anything changed.
bool rewriteNodeReferences(Network& network, const NodeRenames& renames);

} // namespace helix::nt
