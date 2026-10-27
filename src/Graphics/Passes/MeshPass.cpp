#include "Graphics/Passes/MeshPass.h"
#include <glm/vec3.hpp>

namespace helix::gfx {

namespace {

Pipeline createMeshPipeline(
    GraphicsDevice& device,
    MTL::VertexDescriptor* layout,
    NS::UInteger sampleCount,
    const char* name,
    const char* fragmentFunction,
    MTL::PrimitiveTopologyClass topology,
    bool pushedBack
)
{
    PipelineSettings settings;
    settings.name = name;
    settings.vertexFunction = "meshVertex";
    settings.fragmentFunction = fragmentFunction;
    settings.topology = topology;
    settings.vertexLayout = layout;
    settings.depthCompare = MTL::CompareFunctionLessEqual;
    if (pushedBack)
    {
        // Pushes the faces back so the wireframe draws on top.
        settings.depthBias = 1.f;
        settings.slopeScaledDepthBias = 1.f;
    }
    return createPipeline(device, settings, sampleCount);
}

} // namespace

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
        device, layout, sampleCount, "Mesh wireframe", "meshWireframeFragment", MTL::PrimitiveTopologyClassLine, false
    );
}

void MeshPass::upload(MTL::Device* device, const DisplayGeometry& geometry)
{
    const DisplayTopology& topology = geometry.topology;
    positions_ = createGpuArray(device, "Mesh positions", geometry.positions);
    normals_ = createGpuArray(device, "Mesh normals", geometry.normals);

    triangleIndices_ = createGpuArray(device, "Mesh triangles", topology.triangleIndices);
    edgeIndices_ = createGpuArray(device, "Mesh edges", topology.edgeIndices);
    lineIndices_ = createGpuArray(device, "Mesh open faces", topology.lineIndices);
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
