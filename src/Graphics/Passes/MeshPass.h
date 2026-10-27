#pragma once
#include "Graphics/DisplayGeometry.h"
#include "Graphics/Passes/PassSetup.h"

namespace helix::gfx {

/// @brief The display mesh, drawn shaded with an optional wireframe over it.
class MeshPass
{
  public:
    /// @brief Builds the pipelines that draw the mesh.
    ///
    /// @param sampleCount the number of samples the viewport targets hold.
    MeshPass(GraphicsDevice& device, NS::UInteger sampleCount);

    /// @brief Replaces the drawn mesh with the geometry.
    ///
    /// @param retired receives the buffers that were replaced, which a recorded frame may still read.
    void upload(MTL::Device* device, const DisplayGeometry& geometry, RetiredResources& retired);

    /// @brief Draws the mesh with the encoder of the frame.
    void draw(MTL::RenderCommandEncoder* encoder, bool wireframeVisible);

  private:
    /// @brief Draws the indexed vertices with the pipeline.
    void drawIndexed(
        MTL::RenderCommandEncoder* encoder, const Pipeline& pipeline, MTL::PrimitiveType type, const GpuArray& indices
    );

    /// @brief The pipeline that fills the depth of the mesh before it is shaded.
    Pipeline depthPrepassPipeline_;
    Pipeline shadedTrianglePipeline_;
    Pipeline shadedLinePipeline_;
    Pipeline wireframePipeline_;

    GpuArray positions_;
    GpuArray normals_;
    GpuArray triangleIndices_;
    GpuArray edgeIndices_;
    GpuArray lineIndices_;
};

} // namespace helix::gfx
