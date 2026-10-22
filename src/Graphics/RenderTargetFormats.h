#pragma once
#include <GraphicsTypes.h>
#include <RenderDevice.h>

namespace enzo::gfx {

/// @brief The format of the colour target every pass draws into.
inline constexpr Diligent::TEXTURE_FORMAT colorFormat = Diligent::TEX_FORMAT_RGBA8_UNORM;

/// @brief The format of the depth target every pass tests against.
inline constexpr Diligent::TEXTURE_FORMAT depthFormat = Diligent::TEX_FORMAT_D32_FLOAT;

/// @brief The sample count the viewport asks the device for.
///
/// Multisampling smooths the edges of the mesh. A device that does not offer it draws
/// into a single sample instead, which `chooseSampleCount` decides.
inline constexpr Diligent::Uint8 wantedSampleCount = 4;

/// @brief Returns the sample count the device offers for the viewport targets.
///
/// @param device the device the viewport draws with.
///
/// A device that does not offer multisampled textures draws into a single sample, which
/// keeps the viewport working on a device with a smaller feature set.
inline Diligent::Uint8 chooseSampleCount(Diligent::IRenderDevice* device)
{
    if (!device) return 1;
    if (!device->GetAdapterInfo().Texture.Texture2DMSSupported) return 1;
    return wantedSampleCount;
}

} // namespace enzo::gfx
