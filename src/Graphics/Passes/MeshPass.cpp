#include "Graphics/Passes/MeshPass.h"
#include <algorithm>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <limits>
#include <stdexcept>
#include <unordered_set>

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

/// @brief The largest side of a picture the pass makes a texture of; Metal guarantees 16384.
constexpr std::uint32_t kMaxTextureSide = 16384;

/// @brief Whether the picture can become a texture.
bool isUsableImage(const geo::Image* image)
{
    return image && image->width > 0 && image->height > 0 && image->width <= kMaxTextureSide &&
           image->height <= kMaxTextureSide && image->rgba.size() >= std::size_t(image->width) * image->height * 4;
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
    // The texture coordinates come from buffer 2.
    addVertexAttribute(layout, 2, MTL::VertexFormatFloat2, 0, 2);
    addVertexBuffer(layout, 2, sizeof(glm::vec2), false);

    // One sampler for every texture: trilinear with anisotropy, and repeating.
    NS::SharedPtr<MTL::SamplerDescriptor> samplerDescriptor = NS::TransferPtr(MTL::SamplerDescriptor::alloc()->init());
    samplerDescriptor->setMinFilter(MTL::SamplerMinMagFilterLinear);
    samplerDescriptor->setMagFilter(MTL::SamplerMinMagFilterLinear);
    samplerDescriptor->setMipFilter(MTL::SamplerMipFilterLinear);
    samplerDescriptor->setSAddressMode(MTL::SamplerAddressModeRepeat);
    samplerDescriptor->setTAddressMode(MTL::SamplerAddressModeRepeat);
    samplerDescriptor->setMaxAnisotropy(8);
    samplerDescriptor->setLabel(toNsString("Mesh base colour sampler"));
    sampler_ = NS::TransferPtr(device.getDevice()->newSamplerState(samplerDescriptor.get()));

    // The texture bound for the draws that sample nothing, so that every draw has one.
    {
        MTL::TextureDescriptor* white = MTL::TextureDescriptor::texture2DDescriptor(
            MTL::PixelFormatRGBA8Unorm_sRGB, 1, 1, false
        );
        white->setUsage(MTL::TextureUsageShaderRead);
        whiteTexture_ = NS::TransferPtr(device.getDevice()->newTexture(white));
        const std::uint8_t pixel[4] = {255, 255, 255, 255};
        if (whiteTexture_)
        {
            whiteTexture_->setLabel(toNsString("Mesh white texture"));
            whiteTexture_->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, pixel, 4);
        }
    }

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
    // A buffer of texture coordinates as long as the positions, so the vertex stage never reads
    // past its end, whatever geometry was built by hand.
    if (geometry.uvs.size() == geometry.positions.size())
    {
        replaceGpuArray(uvs_, device, "Mesh texture coordinates", geometry.uvs, retired);
    }
    else
    {
        const std::vector<glm::vec2> zeros(geometry.positions.size(), glm::vec2(0.f));
        replaceGpuArray(uvs_, device, "Mesh texture coordinates", zeros, retired);
    }

    replaceGpuArray(triangleIndices_, device, "Mesh triangles", topology.triangleIndices, retired);
    replaceGpuArray(edgeIndices_, device, "Mesh edges", topology.edgeIndices, retired);
    replaceGpuArray(lineIndices_, device, "Mesh open faces", topology.lineIndices, retired);

    // The batches the faces are drawn in. Geometry built without any is drawn whole, with no material.
    materials_ = geometry.materials;
    batches_ = geometry.batches;
    if (batches_.empty() && !topology.triangleIndices.empty())
        batches_.push_back({0, std::uint32_t(topology.triangleIndices.size()), -1});
    for (MaterialBatch& batch : batches_)
        if (batch.material >= std::int32_t(materials_.size())) batch.material = -1;

    // The textures the geometry uses stay, the others are released. Nothing is made here: a
    // texture is made when a frame draws with materials.
    std::unordered_set<const geo::Image*> used;
    hasTexturedMaterial_ = false;
    for (const MaterialBatch& batch : batches_)
    {
        if (batch.material < 0) continue;
        const std::shared_ptr<const geo::Image>& image = materials_[std::size_t(batch.material)]->baseColorTexture;
        if (!isUsableImage(image.get())) continue;
        hasTexturedMaterial_ = true;
        if (used.insert(image.get()).second && !textures_.contains(image.get())) textures_[image.get()].image = image;
    }
    for (auto entry = textures_.begin(); entry != textures_.end();)
    {
        if (used.contains(entry->first))
        {
            ++entry;
            continue;
        }
        if (entry->second.texture) retired.push_back(entry->second.texture);
        entry = textures_.erase(entry);
    }

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

void MeshPass::prepareTextures(MTL::Device* device, MTL::CommandBuffer* commandBuffer, bool materialsVisible)
{
    if (!materialsVisible) return;

    std::vector<MTL::Texture*> created;
    for (auto& [key, entry] : textures_)
    {
        if (entry.attempted) continue;
        entry.attempted = true;

        const geo::Image& image = *entry.image;
        MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::texture2DDescriptor(
            MTL::PixelFormatRGBA8Unorm_sRGB, image.width, image.height, true
        );
        descriptor->setUsage(MTL::TextureUsageShaderRead);
        descriptor->setStorageMode(MTL::StorageModeShared);
        // A texture that cannot be made leaves the material with its factor alone.
        NS::SharedPtr<MTL::Texture> texture = NS::TransferPtr(device->newTexture(descriptor));
        if (!texture) continue;
        texture->setLabel(toNsString("Mesh base colour"));
        texture->replaceRegion(
            MTL::Region::Make2D(0, 0, image.width, image.height), 0, image.rgba.data(), NS::UInteger(image.width) * 4
        );
        entry.texture = texture;
        ++texturesCreated_;
        if (texture->mipmapLevelCount() > 1) created.push_back(texture.get());
    }
    if (created.empty()) return;

    MTL::BlitCommandEncoder* blit = commandBuffer->blitCommandEncoder();
    if (!blit) throw std::runtime_error("The viewport could not build the mip levels of a texture: no blit encoder");
    blit->setLabel(toNsString("Mesh texture mip levels"));
    for (MTL::Texture* texture : created)
        blit->generateMipmaps(texture);
    blit->endEncoding();
}

void MeshPass::bindMaterial(MTL::RenderCommandEncoder* encoder, const geo::Material* material, bool useMaterial)
{
    MaterialConstants constants{};
    MTL::Texture* texture = whiteTexture_.get();
    if (useMaterial && material)
    {
        constants.baseColor = glm::vec4(
            float(material->baseColor.x()), float(material->baseColor.y()), float(material->baseColor.z()), 1.f
        );
        constants.useMaterial = 1;
        if (material->baseColorTexture)
        {
            const auto found = textures_.find(material->baseColorTexture.get());
            if (found != textures_.end() && found->second.texture)
            {
                texture = found->second.texture.get();
                constants.hasTexture = 1;
            }
        }
    }
    encoder->setFragmentBytes(&constants, sizeof(constants), HELIX_MATERIAL_CONSTANTS_SLOT);
    encoder->setFragmentTexture(texture, HELIX_BASE_COLOR_TEXTURE_SLOT);
}

void MeshPass::draw(MTL::RenderCommandEncoder* encoder, bool wireframeVisible, bool materialsVisible)
{
    if (!positions_.buffer || !normals_.buffer || !uvs_.buffer) return;

    encoder->setVertexBuffer(positions_.buffer.get(), 0, 0);
    encoder->setVertexBuffer(normals_.buffer.get(), 0, 1);
    encoder->setVertexBuffer(uvs_.buffer.get(), 0, 2);
    encoder->setFragmentSamplerState(sampler_.get(), HELIX_BASE_COLOR_SAMPLER_SLOT);

    // The depth of the mesh is filled first, so that the pixels a face hides are not
    // shaded.
    drawIndexed(encoder, depthPrepassPipeline_, MTL::PrimitiveTypeTriangle, triangleIndices_);
    if (triangleIndices_.buffer)
    {
        bindPipeline(encoder, shadedTrianglePipeline_);
        if (!materialsVisible)
        {
            bindMaterial(encoder, nullptr, false);
            encoder->drawIndexedPrimitives(
                MTL::PrimitiveTypeTriangle, NS::UInteger(triangleIndices_.count), MTL::IndexTypeUInt32,
                triangleIndices_.buffer.get(), NS::UInteger(0)
            );
        }
        else
        {
            // One draw per run of faces that share a material.
            for (const MaterialBatch& batch : batches_)
            {
                const geo::Material* material = batch.material >= 0 ? materials_[std::size_t(batch.material)].get() : nullptr;
                bindMaterial(encoder, material, material != nullptr);
                encoder->drawIndexedPrimitives(
                    MTL::PrimitiveTypeTriangle, NS::UInteger(batch.indexCount), MTL::IndexTypeUInt32,
                    triangleIndices_.buffer.get(), NS::UInteger(batch.firstIndex) * sizeof(std::uint32_t)
                );
            }
        }
    }
    bindMaterial(encoder, nullptr, false);
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
