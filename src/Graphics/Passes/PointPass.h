#pragma once
#include "Graphics/DisplayGeometry.h"
#include "Graphics/Passes/PassSetup.h"

namespace helix::gfx {

/// @brief The mesh points, drawn as discs facing the camera.
class PointPass
{
  public:
    /// @brief Builds the disc shape and the pipeline that draws it.
    ///
    /// @param sampleCount the number of samples the viewport targets hold.
    PointPass(GraphicsDevice& device, NS::UInteger sampleCount);

    /// @brief Replaces the drawn points with the points of the geometry.
    ///
    /// @param retired receives the buffers that were replaced, which a recorded frame may still read.
    void upload(MTL::Device* device, const DisplayGeometry& geometry, RetiredResources& retired);

    /// @brief Draws the points with the encoder of the frame.
    ///
    /// @param pointsVisible whether every point is drawn. The points that belong to no face are
    /// always drawn.
    void draw(MTL::RenderCommandEncoder* encoder, bool pointsVisible);

  private:
    Pipeline pipeline_;
    GpuArray corners_;
    GpuArray pointPositions_;
    GpuArray soloPointPositions_;
};

} // namespace helix::gfx
