#pragma once
#include "Engine/Network/NodeImpl.h"
#include "Engine/Network/NodeType.h"
#include <boost/config.hpp>
#include <string>

namespace helix::nt {

/// @brief The prefix on the symbol a node library exports for each constructor.
inline constexpr const char* kNodeConstructorPrefix = "helixNode_";

/// @brief Returns the exported symbol backing a constructor named in a manifest.
inline std::string nodeConstructorSymbol(const std::string& constructorName)
{
    return kNodeConstructorPrefix + constructorName;
}

} // namespace helix::nt

/**
 * @brief Exposes a node implementation under the name its manifest asks for.
 *
 * Written once at the bottom of a node's source file. The name is an
 * identifier rather than a string and must match the manifest's
 * implementation constructor, which defaults to the node's own name.
 *
 * ```
 * HELIX_REGISTER_NODE(circle, Circle)
 * ```
 */
#define HELIX_REGISTER_NODE(constructorName, ImplementationClass)                                   \
    extern "C" BOOST_SYMBOL_EXPORT helix::nt::NodeImpl* helixNode_##constructorName(                 \
        helix::nt::Node& node,                                                                      \
        helix::nt::CookContext& context                                                             \
    )                                                                                              \
    {                                                                                              \
        return new ImplementationClass(node, context);                                             \
    }
