#pragma once
#include "Graphics/DisplayGeometry.h"
#include "Graphics/Passes/PassSetup.h"

#include <Buffer.h>
#include <DeviceContext.h>
#include <RefCntAutoPtr.hpp>
#include <RenderDevice.h>

namespace enzo::gfx {

/// @brief The display mesh, drawn shaded with an optional wireframe over it.
class MeshPass
{
  public:
    /// @brief Builds the pipelines that draw the mesh.
    ///
    /// @param frameConstants the buffer the renderer fills with the frame constants.
    /// @param sampleCount the number of samples the viewport targets hold.
    MeshPass(
        Diligent::IRenderDevice* device, Diligent::IBuffer* frameConstants, Diligent::Uint8 sampleCount
    );

    /// @brief Replaces the drawn mesh with the geometry.
    void upload(Diligent::IRenderDevice* device, const DisplayGeometry& geometry);

    /// @brief Draws the mesh into the bound targets.
    void draw(Diligent::IDeviceContext* context, bool wireframeVisible);

  private:
    /// @brief Draws the indexed vertices with the pipeline.
    void drawIndexed(Diligent::IDeviceContext* context, const Pipeline& pipeline, const GpuArray& indices);

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

} // namespace enzo::gfx
