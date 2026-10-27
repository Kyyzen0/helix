#pragma once
#include "Graphics/GraphicsDevice.h"
#include "Graphics/PresentedFrame.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>

// Returns a new device, or skips the test on a machine with no Metal device, such as a
// virtualised CI runner.
//
// Only a missing device skips. A device whose shaders cannot be loaded is a failure, since a
// build that lost its shaders must not pass.
inline std::unique_ptr<helix::gfx::GraphicsDevice> createDeviceOrSkip()
{
    if (!helix::gfx::GraphicsDevice::isMetalAvailable()) SKIP(helix::gfx::GraphicsDevice::noDeviceMessage);
    return std::make_unique<helix::gfx::GraphicsDevice>();
}

/// @brief The four bytes of one pixel of a presented frame, in the order they lie in memory.
struct Rgba
{
    int r, g, b, a;
    bool operator==(const Rgba&) const = default;
};

inline Rgba pixelAt(const helix::gfx::PresentedFrame& frame, std::uint32_t x, std::uint32_t y)
{
    const std::size_t index = (std::size_t(y) * frame.size.x + x) * 4;
    return {frame.pixels[index], frame.pixels[index + 1], frame.pixels[index + 2], frame.pixels[index + 3]};
}

/// @brief Returns whether the pixel is not the colour of the background.
inline bool differsFrom(const Rgba& pixel, const Rgba& background)
{
    return !(pixel == background);
}

/// @brief Returns whether two frames hold the same bytes, without printing them when they do not.
inline bool samePixels(const helix::gfx::PresentedFrame& a, const helix::gfx::PresentedFrame& b)
{
    return a.size == b.size && a.pixels == b.pixels;
}
