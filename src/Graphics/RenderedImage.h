#pragma once
#include <glm/vec2.hpp>

namespace MTL {
class Texture;
}

namespace helix::gfx {

/// @brief A rendered frame a host samples, as the Metal texture it was drawn into.
///
/// @note The texture belongs to the renderer. It stays valid until the renderer is
/// destroyed, and a renderer that changes the size of its frame keeps the texture it
/// replaced alive for a few more frames, so that frames still in flight can sample it.
struct RenderedImage
{
    /// @brief The texture, null when nothing was drawn.
    MTL::Texture* texture = nullptr;

    glm::uvec2 size{0, 0};
};

} // namespace helix::gfx
