#pragma once
#include "Graphics/FrameState.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/ViewportRenderer.h"
#include <QObject>
#include <QSGSimpleTextureNode>
#include <memory>
#include <optional>

class QQuickWindow;
class QSGTexture;

namespace helix::ui {

/// @brief The scene graph node that renders and shows a viewport on the render thread.
///
/// The frame is drawn with Metal into a texture the node owns, and the scene graph samples that
/// texture. Two moments of the render thread are used:
///
/// - `sync`, which runs while the interface thread is blocked, makes the texture fit the item
///   and gives the node a texture to show. Nothing is drawn yet.
/// - `render`, which runs on `QQuickWindow::beforeRendering`, records the frame into the command
///   buffer the scene graph is recording its own frame into. It is the same queue, so the draw
///   comes before the sampling with no event, no fence and no wait.
class ViewportNode : public QObject, public QSGSimpleTextureNode
{
    Q_OBJECT
  public:
    explicit ViewportNode(QQuickWindow* window);
    ~ViewportNode() override;

    /// @brief Takes the frame to draw next and the rectangle to show it in.
    void sync(const gfx::FrameState& frame, const QRectF& rect);

  private:
    /// @brief Draws the pending frame when it differs from the one on screen.
    void render();

    /// @brief Makes the device and the renderer on the Metal device of the scene graph.
    ///
    /// @return whether there is a renderer to draw with.
    bool ensureRenderer();

    /// @brief Drops everything that lives on the GPU, before the scene graph lets go of its device.
    void releaseGraphics();

    /// @brief Shows the texture, wrapping it for the scene graph when it is a new one.
    void showTexture(const gfx::RenderedImage& image, bool hasAlpha);

    QQuickWindow* window_;
    gfx::FrameState pendingFrame_;
    /// @brief The frame the texture holds, empty while it holds none.
    std::optional<gfx::FrameState> renderedFrame_;
    /// @brief Whether the last frame could not be drawn, which is reported once.
    bool failed_ = false;
    /// @brief Whether the scene graph is known not to run on Metal, which is reported once.
    bool unsupported_ = false;

    // The members below are released in reverse order: the textures the scene graph samples
    // first, then the renderer that owns the Metal texture, then the device.
    std::unique_ptr<gfx::GraphicsDevice> device_;
    std::unique_ptr<gfx::ViewportRenderer> renderer_;
    /// @brief A transparent pixel shown until the first frame has a texture.
    std::unique_ptr<QSGTexture> placeholder_;
    /// @brief The texture of the renderer, wrapped for the scene graph.
    std::unique_ptr<QSGTexture> wrapped_;
    /// @brief The Metal texture `wrapped_` shows, to tell when the renderer replaced it.
    const void* shownTexture_ = nullptr;
};

} // namespace helix::ui
