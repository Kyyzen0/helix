#include "Gui/Viewport/ViewportNode.h"
#include "Gui/Viewport/QtGraphicsBridge.h"
#include <QDebug>
#include <QImage>
#include <QQuickWindow>
#include <QSGTexture>
#include <algorithm>
#include <exception>
#include <glm/common.hpp>

namespace helix::ui {

ViewportNode::ViewportNode(QQuickWindow* window) : window_(window)
{
    // The node never owns the texture it shows, so that the order the textures go in is the
    // one of the members rather than the one of the node.
    setOwnsTexture(false);

    // A node with no texture cannot be drawn, so it shows one transparent pixel until the first
    // frame has a texture of its own.
    QImage pixel(1, 1, QImage::Format_ARGB32_Premultiplied);
    pixel.fill(Qt::transparent);
    placeholder_.reset(window->createTextureFromImage(pixel));
    setTexture(placeholder_.get());

    connect(window, &QQuickWindow::beforeRendering, this, &ViewportNode::render, Qt::DirectConnection);

    // The scene graph destroys its Metal device after this signal, so everything that holds a
    // reference to it goes first.
    connect(window, &QQuickWindow::sceneGraphInvalidated, this, &ViewportNode::releaseGraphics, Qt::DirectConnection);
}

ViewportNode::~ViewportNode()
{
    releaseGraphics();
}

void ViewportNode::releaseGraphics()
{
    // Stops showing the textures before they are deleted.
    if (placeholder_) setTexture(placeholder_.get());
    wrapped_.reset();
    shownTexture_ = nullptr;
    renderedFrame_.reset();

    // The renderer releases its textures, and any command buffer still in flight holds its own
    // reference on what it uses.
    renderer_.reset();
    device_.reset();
}

bool ViewportNode::ensureRenderer()
{
    if (renderer_) return true;
    if (unsupported_) return false;

    MTL::Device* metalDevice = QtGraphicsBridge::getDevice(window_);
    if (!metalDevice)
    {
        unsupported_ = true;
        qWarning() << "The viewport needs the Metal scene graph of Qt Quick, which this window does not use.";
        return false;
    }

    try
    {
        device_ = std::make_unique<gfx::GraphicsDevice>(metalDevice);
        renderer_ = std::make_unique<gfx::ViewportRenderer>(*device_);
    }
    catch (const std::exception& error)
    {
        device_.reset();
        unsupported_ = true;
        qWarning().noquote() << "The viewport could not start:" << error.what();
        return false;
    }
    return true;
}

void ViewportNode::sync(const gfx::FrameState& frame, const QRectF& rect)
{
    // A hidden item has no pixels, but the node still needs a texture, so it keeps one pixel.
    gfx::FrameState sized = frame;
    sized.pixelSize = glm::max(sized.pixelSize, glm::uvec2(1, 1));

    if (!(sized == pendingFrame_)) failed_ = false;
    pendingFrame_ = sized;
    setRect(rect);

    if (!ensureRenderer() || failed_) return;

    try
    {
        // Makes the texture fit the item. A new size gives a new texture whose contents are drawn
        // before the scene graph samples it, by `render` of this same frame.
        const gfx::RenderedImage image = renderer_->prepareTarget(pendingFrame_.pixelSize);
        if (static_cast<const void*>(image.texture) != shownTexture_) renderedFrame_.reset();
        showTexture(image, pendingFrame_.backgroundColor.a < 1.f);
    }
    catch (const std::exception& error)
    {
        failed_ = true;
        qWarning().noquote() << "The viewport could not be sized:" << error.what();
    }
}

void ViewportNode::showTexture(const gfx::RenderedImage& image, bool hasAlpha)
{
    if (!image.texture || static_cast<const void*>(image.texture) == shownTexture_) return;

    const QSize size(int(image.size.x), int(image.size.y));
    std::unique_ptr<QSGTexture> wrapper(QtGraphicsBridge::wrapTexture(window_, image.texture, size, hasAlpha));
    if (!wrapper) return;

    // The node switches to the new texture before the old one is deleted.
    setTexture(wrapper.get());
    wrapped_ = std::move(wrapper);
    shownTexture_ = image.texture;
}

void ViewportNode::render()
{
    if (!renderer_ || failed_ || !shownTexture_) return;
    if (renderedFrame_ == pendingFrame_) return;

    try
    {
        const bool recorded = QtGraphicsBridge::recordCommands(window_, [this](MTL::CommandBuffer* commandBuffer) {
            renderer_->encode(pendingFrame_, commandBuffer);
        });

        // A scene graph that gives no command buffer gets a frame drawn on the queue of the
        // device, which is waited for.
        if (!recorded) renderer_->render(pendingFrame_);
        renderedFrame_ = pendingFrame_;
    }
    catch (const std::exception& error)
    {
        failed_ = true;
        qWarning().noquote() << "The viewport could not be drawn:" << error.what();
    }
}

} // namespace helix::ui
