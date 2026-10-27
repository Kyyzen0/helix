#pragma once
#include "Graphics/Passes/PassSetup.h"

namespace helix::gfx {

/// @brief The ground grid drawn under the scene.
class GridPass
{
  public:
    /// @brief Builds the grid lines and the pipeline that draws them.
    ///
    /// @param sampleCount the number of samples the viewport targets hold.
    GridPass(GraphicsDevice& device, NS::UInteger sampleCount);

    /// @brief Draws the grid with the encoder of the frame.
    void draw(MTL::RenderCommandEncoder* encoder);

  private:
    Pipeline pipeline_;
    GpuArray vertices_;
};

} // namespace helix::gfx
