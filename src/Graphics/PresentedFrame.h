#pragma once
#include "Graphics/VulkanImage.h"
#include <cstdint>
#include <cstring>
#include <vector>

namespace enzo::gfx {

/// @brief A rendered frame a host shows.
///
/// A host whose scene graph samples Vulkan takes the image. A host whose scene graph runs
/// on another API, such as the Metal one of macOS, takes the pixels, which are copied back
/// from the device.
struct PresentedFrame
{
    /// @brief The image the frame was drawn into, for a host that samples Vulkan.
    VulkanImage image;

    /// @brief The pixels of the frame, as RGBA8 rows from the top, for a host that cannot
    /// sample the Vulkan image.
    ///
    /// The rows are as wide as the frame in device pixels, with no row padding.
    std::vector<std::uint8_t> pixels;

    /// @brief The size of the frame in device pixels.
    glm::uvec2 size{0, 0};

    /// @brief The number of device pixels per interface pixel.
    float pixelRatio = 1.f;
};

/// @brief Copies the rows of a mapped texture into the pixels of the host.
///
/// @param destination the pixels of the host, which hold `width * height * 4` bytes.
/// @param source the mapped rows of the texture.
/// @param stride the distance between two rows of the texture, in bytes, which the device
/// pads to its own alignment.
/// @param width the width of the frame in pixels.
/// @param height the number of rows of the frame.
///
/// The rows of the texture are padded, so each row is copied on its own.
inline void copyFrameRows(
    std::uint8_t* destination,
    const std::uint8_t* source,
    std::size_t stride,
    std::uint32_t width,
    std::uint32_t height
)
{
    const std::size_t rowBytes = std::size_t(width) * 4;
    for (std::size_t row = 0; row < height; ++row)
    {
        std::memcpy(destination + row * rowBytes, source + row * stride, rowBytes);
    }
}

} // namespace enzo::gfx
