#include "Graphics/Passes/PointPass.h"
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vector>

namespace helix::gfx {

PointPass::PointPass(GraphicsDevice& device, NS::UInteger sampleCount)
{
    ScopedAutoreleasePool pool;

    // Corner buffer
    const std::vector<glm::vec2> corners = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {-0.5f, 0.5f}, {0.5f, 0.5f}};
    corners_ = createGpuArray(device.getDevice(), "Point corners", corners);

    // Reads the disc corners from vertex buffer 0 and one point position per disc from buffer 1.
    MTL::VertexDescriptor* layout = MTL::VertexDescriptor::vertexDescriptor();
    addVertexAttribute(layout, 0, MTL::VertexFormatFloat2, 0, 0);
    addVertexBuffer(layout, 0, sizeof(glm::vec2), false);
    addVertexAttribute(layout, 1, MTL::VertexFormatFloat3, 0, 1);
    addVertexBuffer(layout, 1, sizeof(glm::vec3), true);

    PipelineSettings settings;
    settings.name = "Points";
    settings.vertexFunction = "pointsVertex";
    settings.fragmentFunction = "pointsFragment";
    settings.topology = MTL::PrimitiveTopologyClassTriangle;
    settings.vertexLayout = layout;
    pipeline_ = createPipeline(device, settings, sampleCount);
}

void PointPass::upload(MTL::Device* device, const DisplayGeometry& geometry)
{
    pointPositions_ = createGpuArray(device, "Point positions", geometry.pointPositions);
    soloPointPositions_ = createGpuArray(device, "Solo point positions", geometry.soloPointPositions);
}

void PointPass::draw(MTL::RenderCommandEncoder* encoder, bool pointsVisible)
{
    const GpuArray& positions = pointsVisible ? pointPositions_ : soloPointPositions_;
    if (!positions.buffer) return;

    bindPipeline(encoder, pipeline_);
    encoder->setVertexBuffer(corners_.buffer.get(), 0, 0);
    encoder->setVertexBuffer(positions.buffer.get(), 0, 1);
    encoder->drawPrimitives(
        MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(corners_.count), NS::UInteger(positions.count)
    );
}

} // namespace helix::gfx
