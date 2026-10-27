#pragma once
#include "Graphics/FrameState.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Passes/CameraPrimPass.h"
#include "Graphics/Passes/GridPass.h"
#include "Graphics/Passes/MeshPass.h"
#include "Graphics/Passes/PointPass.h"
#include "Graphics/PresentedFrame.h"
#include "Graphics/RenderedImage.h"
#include <cstddef>
#include <cstdint>
#include <atomic>
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

    /// @brief The number of replaced GPU resources not yet released: the ones waiting for the next
    /// recorded frame and the ones held by a frame the GPU has not finished.
    ///
    /// @note Meant for tests. It may be read from any thread.
    std::size_t retiredResourceCount() const;

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
    /// @brief Resources replaced since the last recorded frame (buffers of the geometry, textures of
    /// the targets). A frame that was recorded earlier but has not finished may still read them, and
    /// a host command buffer does not retain what it reads, so the next recorded frame takes them
    /// and keeps them until the GPU has finished it.
    RetiredResources retired_;
    /// @brief How many resources the command buffers in flight hold. Shared with their handlers,
    /// which can outlive the renderer.
    std::shared_ptr<std::atomic<std::size_t>> inFlightCount_;
    /// @brief The geometry the passes hold on the GPU.
    std::shared_ptr<const DisplayGeometry> uploadedGeometry_;
};

} // namespace helix::gfx
