#pragma once
#include "Graphics/FrameState.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Passes/CameraPrimPass.h"
#include "Graphics/Passes/GridPass.h"
#include "Graphics/Passes/MeshPass.h"
#include "Graphics/Passes/PointPass.h"
#include "Graphics/PresentedFrame.h"
#include "Graphics/RenderedImage.h"
#include <cstdint>
#include <deque>
#include <memory>

namespace helix::gfx {

/// @brief A renderer that draws viewport frames into a Metal texture a host samples.
///
/// The pipelines are built once, when the renderer is made, and the geometry stays in its
/// buffers until a frame brings new geometry, so a frame whose only change is the camera or a
/// colour builds nothing and copies nothing.
///
/// A renderer is used from one thread at a time.
class ViewportRenderer
{
  public:
    explicit ViewportRenderer(GraphicsDevice& device);
    ~ViewportRenderer();

    ViewportRenderer(const ViewportRenderer&) = delete;
    ViewportRenderer& operator=(const ViewportRenderer&) = delete;

    /// @brief Draws one frame on the queue of the device, waits for it, and returns the texture.
    ///
    /// @note The texture changes whenever the frame size does.
    /// @throws std::runtime_error when the frame could not be drawn.
    RenderedImage render(const FrameState& frame);

    /// @brief Draws one frame like `render` and returns it as pixels as well.
    PresentedFrame renderToPixels(const FrameState& frame);

    /// @brief Records one frame into the command buffer of the host and returns its texture.
    ///
    /// Nothing is committed and nothing is waited for. The host commits the command buffer, and
    /// samples the texture in a later pass of the same queue, which Metal orders after this one.
    ///
    /// @param commandBuffer a command buffer with no encoder open.
    /// @throws std::runtime_error when the frame could not be recorded.
    RenderedImage encode(const FrameState& frame, MTL::CommandBuffer* commandBuffer);

    /// @brief Makes the targets fit the size and returns the texture the next frame is drawn into.
    ///
    /// A host calls it before it draws, to have a texture to show. The contents are undefined until
    /// a frame is drawn.
    ///
    /// @throws std::runtime_error when the targets could not be allocated.
    RenderedImage prepareTarget(glm::uvec2 size);

  private:
    /// @brief The textures one frame size is drawn with.
    struct Targets
    {
        /// @brief The multisampled target the passes draw into, null with a single sample.
        NS::SharedPtr<MTL::Texture> multisample;
        /// @brief The depth the passes test against.
        NS::SharedPtr<MTL::Texture> depth;
        /// @brief The single sample texture the host samples.
        NS::SharedPtr<MTL::Texture> color;
        glm::uvec2 size{0, 0};
    };

    /// @brief A set of targets that was replaced and is kept for the frames still in flight.
    struct RetiredTargets
    {
        Targets targets;
        std::uint64_t retiredAtFrame = 0;
    };

    Targets createTargets(glm::uvec2 size) const;
    NS::SharedPtr<MTL::Texture> createTexture(
        glm::uvec2 size, MTL::PixelFormat format, NS::UInteger samples, MTL::StorageMode storage, MTL::TextureUsage usage, const char* name
    ) const;

    /// @brief Uploads the geometry of the frame when it is not the one on the GPU.
    void uploadGeometry(const FrameState& frame);

    GraphicsDevice& device_;
    NS::UInteger sampleCount_ = 1;
    GridPass gridPass_;
    MeshPass meshPass_;
    PointPass pointPass_;
    CameraPrimPass cameraPrimPass_;
    Targets targets_;
    std::deque<RetiredTargets> retiredTargets_;
    std::uint64_t frameIndex_ = 0;
    /// @brief The geometry the passes hold on the GPU.
    std::shared_ptr<const DisplayGeometry> uploadedGeometry_;
};

} // namespace helix::gfx
