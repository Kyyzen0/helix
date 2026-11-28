#include "Graphics/Passes/GridPass.h"
#include "Graphics/GridScale.h"
#include <glm/vec3.hpp>
#include <vector>

namespace helix::gfx {

namespace {

// What a vertex says about its line: the shader draws a minor line, a major one, or one of the two
// lines through the origin, each with its own strength and tint.
constexpr float minorLine = 0.f;
constexpr float majorLine = 1.f;
constexpr float xAxisLine = 2.f;
constexpr float zAxisLine = 3.f;

/// @brief Returns the endpoints of the grid lines, two per line, as (x, z, kind).
///
/// The positions are in cells from the centre of the grid: the shader scales them by the cell
/// size of the frame, so one buffer serves every zoom. The axes are drawn on top of the lines the
/// grid has at the origin: their vertices carry the kind, and the shader puts them on the world
/// axis, wherever the centre of the grid is.
std::vector<glm::vec3> buildGridVertices()
{
    std::vector<glm::vec3> vertices;
    const float half = float(gridHalfCells);
    for (int index = -gridHalfCells; index <= gridHalfCells; ++index)
    {
        const float kind = index % gridMajorEvery == 0 ? majorLine : minorLine;
        const float offset = float(index);
        // Runs one line along z and one along x at the same offset.
        vertices.emplace_back(offset, -half, kind);
        vertices.emplace_back(offset, half, kind);
        vertices.emplace_back(-half, offset, kind);
        vertices.emplace_back(half, offset, kind);
    }
    vertices.emplace_back(-half, 0.f, xAxisLine);
    vertices.emplace_back(half, 0.f, xAxisLine);
    vertices.emplace_back(0.f, -half, zAxisLine);
    vertices.emplace_back(0.f, half, zAxisLine);
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
