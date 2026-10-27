#pragma once
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Metal.h"
#include "Graphics/Shaders/ShaderTypes.h"
#include <cstdint>
#include <vector>

namespace helix::gfx {

/// @brief A pipeline of a pass with the depth state and depth bias it draws with.
///
/// Metal keeps the depth bias on the command encoder rather than in the pipeline, so it
/// is stored here and set whenever the pipeline is bound.
struct Pipeline
{
    NS::SharedPtr<MTL::RenderPipelineState> state;
    NS::SharedPtr<MTL::DepthStencilState> depthState;
    float depthBias = 0.f;
    float slopeScaledDepthBias = 0.f;
};

/// @brief The parts that differ between the pipelines of the passes.
struct PipelineSettings
{
    const char* name = "";
    const char* vertexFunction = "";
    /// @brief The fragment function, or null for a pipeline that writes depth and no colour.
    const char* fragmentFunction = nullptr;
    MTL::PrimitiveTopologyClass topology = MTL::PrimitiveTopologyClassTriangle;
    /// @brief How the vertex buffers are laid out.
    MTL::VertexDescriptor* vertexLayout = nullptr;
    /// @brief The comparison of the depth test, or `Always` with no write to skip the test.
    MTL::CompareFunction depthCompare = MTL::CompareFunctionLess;
    bool depthWrite = true;
    /// @brief Whether the colour is blended over the target by its alpha, leaving the alpha of the target alone.
    bool blendOver = false;
    /// @brief The depth bias of the faces, so a wireframe drawn over them wins the depth test.
    float depthBias = 0.f;
    float slopeScaledDepthBias = 0.f;
};

/// @brief Returns a pipeline that draws into the viewport targets.
///
/// @param sampleCount the number of samples the viewport targets hold.
///
/// @throws std::runtime_error when a shader function is missing or the pipeline is refused.
Pipeline createPipeline(GraphicsDevice& device, const PipelineSettings& settings, NS::UInteger sampleCount);

/// @brief Binds the pipeline, its depth state and its depth bias for the next draws.
void bindPipeline(MTL::RenderCommandEncoder* encoder, const Pipeline& pipeline);

/// @brief Describes a float vertex attribute that reads buffer `bufferIndex` at `offset`.
void addVertexAttribute(
    MTL::VertexDescriptor* layout,
    NS::UInteger attribute,
    MTL::VertexFormat format,
    NS::UInteger offset,
    NS::UInteger bufferIndex
);

/// @brief Describes how buffer `bufferIndex` is stepped through, per vertex or per instance.
void addVertexBuffer(MTL::VertexDescriptor* layout, NS::UInteger bufferIndex, NS::UInteger stride, bool perInstance);

/// @brief An unchanging GPU buffer and the number of values it holds.
struct GpuArray
{
    /// @brief The buffer, null when it holds no values.
    NS::SharedPtr<MTL::Buffer> buffer;
    std::uint32_t count = 0;
};

/// @brief Returns a buffer holding the bytes, shared with the CPU so that no staging copy is made.
///
/// @throws std::runtime_error when the bytes are larger than the device allows in a buffer.
NS::SharedPtr<MTL::Buffer> createSharedBuffer(MTL::Device* device, const char* name, const void* bytes, std::size_t size);

/// @brief Returns a GPU array holding the values.
template <typename Value>
GpuArray createGpuArray(MTL::Device* device, const char* name, const std::vector<Value>& values)
{
    GpuArray array;
    array.count = std::uint32_t(values.size());
    if (values.empty()) return array;

    array.buffer = createSharedBuffer(device, name, values.data(), values.size() * sizeof(Value));
    return array;
}

} // namespace helix::gfx
