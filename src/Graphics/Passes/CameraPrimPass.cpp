#include "Graphics/Passes/CameraPrimPass.h"
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vector>

namespace helix::gfx {

namespace {

constexpr float frameHalfWidth = 0.5f;
constexpr float frameHalfHeight = 0.375f;

/// @brief Returns the edges of the camera frame, two endpoints per edge.
std::vector<glm::vec3> buildFrameVertices()
{
    const glm::vec3 bottomLeft{-frameHalfWidth, -frameHalfHeight, 0.f};
    const glm::vec3 bottomRight{frameHalfWidth, -frameHalfHeight, 0.f};
    const glm::vec3 topRight{frameHalfWidth, frameHalfHeight, 0.f};
    const glm::vec3 topLeft{-frameHalfWidth, frameHalfHeight, 0.f};
    return {bottomLeft, bottomRight, bottomRight, topRight, topRight, topLeft, topLeft, bottomLeft};
}

} // namespace

CameraPrimPass::CameraPrimPass(GraphicsDevice& device, NS::UInteger sampleCount)
{
    ScopedAutoreleasePool pool;

    // Frame vertex buffer
    frameVertices_ = createGpuArray(device.getDevice(), "Camera frame vertices", buildFrameVertices());

    // Reads the frame vertices from vertex buffer 0 and one transform per camera, column by
    // column, from buffer 1.
    MTL::VertexDescriptor* layout = MTL::VertexDescriptor::vertexDescriptor();
    addVertexAttribute(layout, 0, MTL::VertexFormatFloat3, 0, 0);
    addVertexBuffer(layout, 0, sizeof(glm::vec3), false);
    for (NS::UInteger column = 0; column < 4; ++column)
        addVertexAttribute(layout, 1 + column, MTL::VertexFormatFloat4, column * sizeof(glm::vec4), 1);
    addVertexBuffer(layout, 1, sizeof(glm::mat4), true);

    PipelineSettings settings;
    settings.name = "Camera primitives";
    settings.vertexFunction = "cameraPrimVertex";
    settings.fragmentFunction = "cameraPrimFragment";
    settings.topology = MTL::PrimitiveTopologyClassLine;
    settings.vertexLayout = layout;
    pipeline_ = createPipeline(device, settings, sampleCount);
}

void CameraPrimPass::upload(MTL::Device* device, const DisplayGeometry& geometry, RetiredResources& retired)
{
    replaceGpuArray(transforms_, device, "Camera transforms", geometry.cameraTransforms, retired);
}

void CameraPrimPass::draw(MTL::RenderCommandEncoder* encoder, std::optional<std::size_t> hiddenCamera)
{
    if (!transforms_.buffer) return;

    bindPipeline(encoder, pipeline_);
    encoder->setVertexBuffer(frameVertices_.buffer.get(), 0, 0);
    encoder->setVertexBuffer(transforms_.buffer.get(), 0, 1);

    if (!hiddenCamera.has_value())
    {
        drawRange(encoder, 0, transforms_.count);
        return;
    }
    drawRange(encoder, 0, *hiddenCamera);
    drawRange(encoder, *hiddenCamera + 1, transforms_.count);
}

void CameraPrimPass::drawRange(MTL::RenderCommandEncoder* encoder, std::size_t first, std::size_t end)
{
    if (first >= end) return;

    // The first instance moves where the per camera transforms start being read from.
    encoder->drawPrimitives(
        MTL::PrimitiveTypeLine,
        NS::UInteger(0),
        NS::UInteger(frameVertices_.count),
        NS::UInteger(end - first),
        NS::UInteger(first)
    );
}

} // namespace helix::gfx
