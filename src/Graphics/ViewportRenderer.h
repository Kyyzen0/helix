#pragma once
#include "Graphics/FrameState.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/PresentedFrame.h"
#include "Graphics/VulkanImage.h"
#include <memory>
#include <vector>

namespace enzo::gfx {

/// @brief A renderer that draws viewport frames into an image a host samples.
class ViewportRenderer
{
  public:
    explicit ViewportRenderer(GraphicsDevice& device);
    ~ViewportRenderer();

    ViewportRenderer(const ViewportRenderer&) = delete;
    ViewportRenderer& operator=(const ViewportRenderer&) = delete;

    /// @brief Draws one frame and returns the image it was drawn into.
    ///
    /// @note The image changes whenever the frame size does.
    VulkanImage render(const FrameState& frame);

    /// @brief Draws one frame and returns it as pixels a host can show.
    ///
    /// A host whose scene graph cannot sample a Vulkan image, such as the Metal one of
    /// macOS, shows the pixels instead. They are copied back from the device, which waits
    /// for the frame to be drawn.
    PresentedFrame renderToPixels(const FrameState& frame);

  private:
    /// @brief Draws one frame into the targets of the viewport and returns the image.
    ///
    /// @param frame the frame to draw.
    /// @param readPixels whether the frame is copied back into the pixels of the host.
    ///
    /// @throws std::runtime_error when the frame could not be drawn.
    VulkanImage draw(const FrameState& frame, bool readPixels);

    /// @brief Draws the frame into the targets of the viewport and returns the image.
    VulkanImage drawFrame(const FrameState& frame, bool readPixels);

    /// @brief Copies the image of the viewport back into the pixels of the host.
    void readBack(const FrameState& frame);

    /// @brief Makes the targets of the viewport fit the size of the frame.
    void resizeTargets(const FrameState& frame);

    struct DiligentObjects;
    GraphicsDevice& device_;
    std::unique_ptr<DiligentObjects> diligent_;
    /// @brief The geometry the mesh pass holds on the GPU.
    std::shared_ptr<const DisplayGeometry> uploadedGeometry_;
    /// @brief The pixels of the last frame that was read back.
    std::vector<std::uint8_t> readPixels_;
};

} // namespace enzo::gfx
