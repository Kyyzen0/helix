#include "Graphics/Passes/GridPass.h"
#include <glm/vec3.hpp>
#include <vector>

namespace helix::gfx {

namespace {

constexpr int lineCount = 40;
constexpr float lineSpacing = 2.f;
constexpr float halfLength = 50.f;

/// @brief Returns the endpoints of the grid lines on the ground plane, two per line.
std::vector<glm::vec3> buildGridVertices()
{
    std::vector<glm::vec3> vertices;
    const float firstOffset = -0.5f * (lineCount - 1) * lineSpacing;
    for (int lineIndex = 0; lineIndex < lineCount; ++lineIndex)
    {
        const float offset = firstOffset + lineIndex * lineSpacing;

        // Runs one line along z and one along x at the same offset.
        vertices.emplace_back(offset, 0.f, -halfLength);
        vertices.emplace_back(offset, 0.f, halfLength);
        vertices.emplace_back(-halfLength, 0.f, offset);
        vertices.emplace_back(halfLength, 0.f, offset);
    }
    return vertices;
}

} // namespace

GridPass::GridPass(GraphicsDevice& device, NS::UInteger sampleCount)
{
    ScopedAutoreleasePool pool;
    vertices_ = createGpuArray(device.getDevice(), "Grid vertices", buildGridVertices());

    // Reads the position from vertex buffer 0.
    MTL::VertexDescriptor* layout = MTL::VertexDescriptor::vertexDescriptor();
    addVertexAttribute(layout, 0, MTL::VertexFormatFloat3, 0, 0);
    addVertexBuffer(layout, 0, sizeof(glm::vec3), false);

    PipelineSettings settings;
    settings.name = "Grid";
    settings.vertexFunction = "gridVertex";
    settings.fragmentFunction = "gridFragment";
    settings.topology = MTL::PrimitiveTopologyClassLine;
    settings.vertexLayout = layout;
    // Skips the depth test and write so the mesh always draws over the grid.
    settings.depthCompare = MTL::CompareFunctionAlways;
    settings.depthWrite = false;
    settings.blendOver = true;
    pipeline_ = createPipeline(device, settings, sampleCount);
}

void GridPass::draw(MTL::RenderCommandEncoder* encoder)
{
    bindPipeline(encoder, pipeline_);
    encoder->setVertexBuffer(vertices_.buffer.get(), 0, 0);
    encoder->drawPrimitives(MTL::PrimitiveTypeLine, NS::UInteger(0), NS::UInteger(vertices_.count));
}

} // namespace helix::gfx
