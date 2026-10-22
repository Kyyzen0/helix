#include "Gui/Viewport/ViewportNode.h"
#include "Gui/Viewport/QtGraphicsBridge.h"
#include <QQuickWindow>

namespace enzo::ui {

ViewportNode::ViewportNode(QQuickWindow* window)
    : window_(window)
{
    setOwnsTexture(true);
#ifndef __APPLE__
    renderer_.emplace(gfx::GraphicsDevice::get());
#endif
    connect(window, &QQuickWindow::beforeRendering, this, &ViewportNode::render, Qt::DirectConnection);
}

void ViewportNode::sync(const gfx::FrameState& frame, const QRectF& rect)
{
    pendingFrame_ = frame;
    setRect(rect);
}

void ViewportNode::render()
{
#ifndef __APPLE__
    const bool hasPixels = pendingFrame_.pixelSize.x > 0 && pendingFrame_.pixelSize.y > 0;
    const bool upToDate = texture() && pendingFrame_ == renderedFrame_;
    if (!hasPixels || upToDate) return;

    window_->beginExternalCommands();
    const gfx::VulkanImage image = renderer_->render(pendingFrame_);
    window_->endExternalCommands();
    renderedFrame_ = pendingFrame_;

    if (image.image != shownImage_)
    {
        setTexture(QtGraphicsBridge::wrapImage(window_, image));
        shownImage_ = image.image;
    }
#endif
    // The scene graph of macOS runs on Metal and cannot show an image of the
    // graphics device, which draws through Vulkan. The node stays empty
    // there, and the device is not asked to draw at all.
}

} // namespace enzo::ui
