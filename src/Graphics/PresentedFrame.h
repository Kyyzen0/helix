#pragma once
#include "Graphics/RenderedImage.h"
#include <cstdint>
#include <glm/vec2.hpp>
#include <vector>

namespace helix::gfx {

/// @brief A rendered frame copied back to the CPU.
///
/// A host whose scene graph runs on Metal samples `image` directly. The pixels are for
/// a host that wants the frame on the CPU, such as a test or an export, and they cost a
/// wait for the frame to be drawn.
struct PresentedFrame
{
    /// @brief The texture the frame was drawn into.
    RenderedImage image;

    /// @brief The pixels of the frame, as RGBA8 rows from the top.
    ///
    /// The rows are as wide as the frame in device pixels, with no row padding.
    std::vector<std::uint8_t> pixels;

    /// @brief The size of the frame in device pixels.
    glm::uvec2 size{0, 0};

    /// @brief The number of device pixels per interface pixel.
    float pixelRatio = 1.f;
};

} // namespace helix::gfx
