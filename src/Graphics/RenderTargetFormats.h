#pragma once
#include "Graphics/Metal.h"

namespace helix::gfx {

/// @brief The format of the colour target every pass draws into.
inline constexpr MTL::PixelFormat colorFormat = MTL::PixelFormatRGBA8Unorm;

/// @brief The format of the depth target every pass tests against.
inline constexpr MTL::PixelFormat depthFormat = MTL::PixelFormatDepth32Float;

/// @brief The sample count the viewport asks the device for.
///
/// Multisampling smooths the edges of the mesh. A device that does not offer it draws
/// into a single sample instead, which `chooseSampleCount` decides.
inline constexpr NS::UInteger wantedSampleCount = 4;

/// @brief Returns the sample count the device offers for the viewport targets.
///
/// Every Apple GPU offers 4 samples, so this only falls back on a device that does not.
inline NS::UInteger chooseSampleCount(MTL::Device* device)
{
    if (!device) return 1;
    return device->supportsTextureSampleCount(wantedSampleCount) ? wantedSampleCount : 1;
}

} // namespace helix::gfx
