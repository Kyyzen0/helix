#include "Graphics/Passes/MeshPass.h"
#include <algorithm>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <limits>

namespace helix::gfx {

namespace {

Pipeline createMeshPipeline(
    GraphicsDevice& device,
    MTL::VertexDescriptor* layout,
    NS::UInteger sampleCount,
    const char* name,
    const char* fragmentFunction,
    MTL::PrimitiveTopologyClass topology,
    bool pushedBack,
    bool blended = false
)
{
    PipelineSettings settings;
    settings.name = name;
    settings.vertexFunction = "meshVertex";
    settings.fragmentFunction = fragmentFunction;
    settings.topology = topology;
    settings.vertexLayout = layout;
    settings.depthCompare = MTL::CompareFunctionLessEqual;
    settings.blendOver = blended;
    if (pushedBack)
    {
        // Pushes the faces back so the wireframe draws on top.
        settings.depthBias = 1.f;
        settings.slopeScaledDepthBias = 1.f;
    }
    return createPipeline(device, settings, sampleCount);
}

} // namespace

float adaptiveWireframeOpacity(float edgePoints)
{
    const float t = std::clamp(
        (edgePoints - kWireframeGoneEdgePoints) / (kWireframeFullEdgePoints - kWireframeGoneEdgePoints), 0.f, 1.f
    );
    return t * t * (3.f - 2.f * t);
}

MeshPass::MeshPass(GraphicsDevice& device, NS::UInteger sampleCount)
{
    ScopedAutoreleasePool pool;

    // Reads positions from vertex buffer 0 and normals from buffer 1.
    MTL::VertexDescriptor* layout = MTL::VertexDescriptor::vertexDescriptor();
    addVertexAttribute(layout, 0, MTL::VertexFormatFloat3, 0, 0);
    addVertexBuffer(layout, 0, sizeof(glm::vec3), false);
    addVertexAttribute(layout, 1, MTL::VertexFormatFloat3, 0, 1);
    addVertexBuffer(layout, 1, sizeof(glm::vec3), false);

    // The prepass fills the depth of the mesh before it is shaded, so that the pixels a
    // face hides are never shaded at all. It takes the same depth bias as the faces, so
    // that the depth it writes is the depth the faces are drawn at.
    depthPrepassPipeline_ = createMeshPipeline(
        device, layout, sampleCount, "Mesh depth prepass", nullptr, MTL::PrimitiveTopologyClassTriangle, true
    );
    shadedTrianglePipeline_ = createMeshPipeline(
        device, layout, sampleCount, "Mesh faces", "meshShadedFragment", MTL::PrimitiveTopologyClassTriangle, true
    );
    shadedLinePipeline_ = createMeshPipeline(
        device, layout, sampleCount, "Mesh open faces", "meshShadedFragment", MTL::PrimitiveTopologyClassLine, false
    );
    wireframePipeline_ = createMeshPipeline(
        device, layout, sampleCount, "Mesh wireframe", "meshWireframeFragment", MTL::PrimitiveTopologyClassLine, false, true
    );
}

void MeshPass::upload(MTL::Device* device, const DisplayGeometry& geometry, RetiredResources& retired)
{
    const DisplayTopology& topology = geometry.topology;
    replaceGpuArray(positions_, device, "Mesh positions", geometry.positions, retired);
    replaceGpuArray(normals_, device, "Mesh normals", geometry.normals, retired);

    replaceGpuArray(triangleIndices_, device, "Mesh triangles", topology.triangleIndices, retired);
    replaceGpuArray(edgeIndices_, device, "Mesh edges", topology.edgeIndices, retired);
    replaceGpuArray(lineIndices_, device, "Mesh open faces", topology.lineIndices, retired);

    // What the adaptive wireframe judges the density of the mesh by. A denser mesh is not drawn
    // with a wireframe, so it is not measured.
    triangleCount_ = topology.triangleIndices.size() / 3;
    meanEdgeLength_ = 0.f;
    center_ = glm::vec3(0.f);
    if (triangleCount_ == 0 || triangleCount_ > kWireframeAutoTriangleLimit) return;

    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    for (const glm::vec3& point : geometry.positions)
    {
        minimum = glm::min(minimum, point);
        maximum = glm::max(maximum, point);
    }
    if (!geometry.positions.empty()) center_ = (minimum + maximum) * 0.5f;

    double total = 0.0;
    std::size_t edges = 0;
    const std::vector<std::uint32_t>& indices = topology.edgeIndices;
    for (std::size_t i = 0; i + 1 < indices.size(); i += 2)
    {
        if (indices[i] >= geometry.positions.size() || indices[i + 1] >= geometry.positions.size()) continue;
        total += glm::distance(geometry.positions[indices[i]], geometry.positions[indices[i + 1]]);
        ++edges;
    }
    if (edges > 0) meanEdgeLength_ = static_cast<float>(total / double(edges));
}

void MeshPass::draw(MTL::RenderCommandEncoder* encoder, bool wireframeVisible)
{
    if (!positions_.buffer || !normals_.buffer) return;

    encoder->setVertexBuffer(positions_.buffer.get(), 0, 0);
    encoder->setVertexBuffer(normals_.buffer.get(), 0, 1);

    // The depth of the mesh is filled first, so that the pixels a face hides are not
    // shaded.
    drawIndexed(encoder, depthPrepassPipeline_, MTL::PrimitiveTypeTriangle, triangleIndices_);
    drawIndexed(encoder, shadedTrianglePipeline_, MTL::PrimitiveTypeTriangle, triangleIndices_);
    drawIndexed(encoder, shadedLinePipeline_, MTL::PrimitiveTypeLine, lineIndices_);
    if (wireframeVisible) drawIndexed(encoder, wireframePipeline_, MTL::PrimitiveTypeLine, edgeIndices_);
}

void MeshPass::drawIndexed(
    MTL::RenderCommandEncoder* encoder, const Pipeline& pipeline, MTL::PrimitiveType type, const GpuArray& indices
)
{
    if (!indices.buffer) return;

    bindPipeline(encoder, pipeline);
    encoder->drawIndexedPrimitives(
        type, NS::UInteger(indices.count), MTL::IndexTypeUInt32, indices.buffer.get(), NS::UInteger(0)
    );
}

} // namespace helix::gfx
