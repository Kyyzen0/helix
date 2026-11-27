#pragma once
#include "Graphics/DisplayGeometry.h"
#include "Graphics/Passes/PassSetup.h"
#include <cstddef>
#include <glm/vec3.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

namespace helix::gfx {

/// @brief The most triangles a mesh can have for the adaptive wireframe to be drawn at all.
///
/// Past it the edges are denser than the pixels of any ordinary view, so the lines would cover
/// the shading instead of adding to it.
inline constexpr std::size_t kWireframeAutoTriangleLimit = 50000;

/// @brief The opacity of the adaptive wireframe for edges that are `edgePoints` apart on screen.
///
/// @param edgePoints the mean length of an edge in points of the interface (device pixels
/// divided by the pixel ratio). It is fully opaque from kWireframeFullEdgePoints and fades out
/// to nothing at kWireframeGoneEdgePoints, where the edges touch.
float adaptiveWireframeOpacity(float edgePoints);

inline constexpr float kWireframeGoneEdgePoints = 2.f;
inline constexpr float kWireframeFullEdgePoints = 6.f;

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

    /// @brief Makes the textures the uploaded materials need, when `materialsVisible`.
    ///
    /// Opens a blit encoder on the command buffer to build the mip levels of the textures it
    /// creates, and closes it before it returns, so it runs before a render encoder is opened.
    /// Creates nothing when the materials are hidden or when every texture already exists.
    void prepareTextures(MTL::Device* device, MTL::CommandBuffer* commandBuffer, bool materialsVisible);

    /// @brief Draws the mesh with the encoder of the frame.
    ///
    /// @param materialsVisible whether the faces are shaded with their materials; when false
    /// every face takes the colour of the frame.
    void draw(MTL::RenderCommandEncoder* encoder, bool wireframeVisible, bool materialsVisible);

    /// @brief Whether a material of the uploaded mesh has a base colour texture.
    bool hasTexturedMaterial() const { return hasTexturedMaterial_; }

    /// @brief The number of textures the pass holds on the GPU.
    std::size_t textureCount() const { return textures_.size(); }
    /// @brief The number of textures the pass has created since it was made.
    std::size_t texturesCreated() const { return texturesCreated_; }

    /// @brief The number of triangles of the uploaded mesh.
    std::size_t triangleCount() const { return triangleCount_; }

    /// @brief The mean length of the edges of the uploaded mesh in world units, and 0 when the
    /// mesh has more triangles than the adaptive wireframe is drawn for (it is not measured).
    float meanEdgeLength() const { return meanEdgeLength_; }

    /// @brief The centre of the bounding box of the uploaded mesh.
    glm::vec3 center() const { return center_; }

  private:
    /// @brief A picture of a material on the GPU, kept for as long as the geometry uses it.
    struct TextureEntry
    {
        /// @brief Keeps the pixels the key points to alive, so the key cannot be reused.
        std::shared_ptr<const geo::Image> image;
        /// @brief Null until it is made, or when it could not be.
        NS::SharedPtr<MTL::Texture> texture;
        bool attempted = false;
    };

    /// @brief Binds the material of a draw: its constants, its texture and the sampler.
    void bindMaterial(MTL::RenderCommandEncoder* encoder, const geo::Material* material, bool useMaterial);

    /// @brief Draws the indexed vertices with the pipeline.
    void drawIndexed(
        MTL::RenderCommandEncoder* encoder, const Pipeline& pipeline, MTL::PrimitiveType type, const GpuArray& indices
    );

    /// @brief The pipeline that fills the depth of the mesh before it is shaded.
    Pipeline depthPrepassPipeline_;
    Pipeline shadedTrianglePipeline_;
    Pipeline shadedLinePipeline_;
    Pipeline wireframePipeline_;

    NS::SharedPtr<MTL::SamplerState> sampler_;
    /// @brief The texture bound for draws that sample nothing.
    NS::SharedPtr<MTL::Texture> whiteTexture_;
    std::unordered_map<const geo::Image*, TextureEntry> textures_;
    std::size_t texturesCreated_ = 0;
    /// @brief The materials of the uploaded geometry, which the batches point into.
    std::vector<std::shared_ptr<const geo::Material>> materials_;
    std::vector<MaterialBatch> batches_;
    bool hasTexturedMaterial_ = false;

    GpuArray positions_;
    GpuArray normals_;
    GpuArray uvs_;
    GpuArray triangleIndices_;
    GpuArray edgeIndices_;
    GpuArray lineIndices_;

    std::size_t triangleCount_ = 0;
    float meanEdgeLength_ = 0.f;
    glm::vec3 center_{0.f};
};

} // namespace helix::gfx
