#pragma once
#include "Graphics/DisplayGeometry.h"
#include "Graphics/Passes/PassSetup.h"
#include <cstddef>
#include <optional>

namespace helix::gfx {

/// @brief The camera primitives, drawn as rectangular frames.
class CameraPrimPass
{
  public:
    /// @brief Builds the frame shape and the pipeline that draws it.
    ///
    /// @param sampleCount the number of samples the viewport targets hold.
    CameraPrimPass(GraphicsDevice& device, NS::UInteger sampleCount);

    /// @brief Replaces the drawn cameras with the cameras of the geometry.
    void upload(MTL::Device* device, const DisplayGeometry& geometry);

    /// @brief Draws the cameras with the encoder of the frame.
    ///
    /// @param hiddenCamera the index of a camera left out, or empty to draw every camera.
    void draw(MTL::RenderCommandEncoder* encoder, std::optional<std::size_t> hiddenCamera);

  private:
    /// @brief Draws the cameras from the first index up to but not including the end index.
    void drawRange(MTL::RenderCommandEncoder* encoder, std::size_t first, std::size_t end);

    Pipeline pipeline_;
    GpuArray frameVertices_;
    GpuArray transforms_;
};

} // namespace helix::gfx
