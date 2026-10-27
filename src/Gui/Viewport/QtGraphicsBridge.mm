// Objective-C++ since the scene graph takes and hands out Metal objects as `id<...>`, and its
// native texture interface is only declared for Objective-C.
#include "Gui/Viewport/QtGraphicsBridge.h"

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtQuick/qsgtexture_platform.h>

#import <Metal/Metal.h>

namespace helix::ui {

void QtGraphicsBridge::selectMetal()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
}

bool QtGraphicsBridge::usesMetal(QQuickWindow* window)
{
    const QSGRendererInterface* rendererInterface = window ? window->rendererInterface() : nullptr;
    return rendererInterface && rendererInterface->graphicsApi() == QSGRendererInterface::Metal;
}

MTL::Device* QtGraphicsBridge::getDevice(QQuickWindow* window)
{
    if (!usesMetal(window)) return nullptr;

    // The device resource is the `id<MTLDevice>` the scene graph draws with, and metal-cpp's
    // MTL::Device is the same object.
    void* device = window->rendererInterface()->getResource(window, QSGRendererInterface::DeviceResource);
    return static_cast<MTL::Device*>(device);
}

bool QtGraphicsBridge::recordCommands(QQuickWindow* window, const std::function<void(MTL::CommandBuffer*)>& record)
{
    if (!usesMetal(window)) return false;

    // The command list resource is the `id<MTLCommandBuffer>` the frame is recorded into.
    void* commandList = window->rendererInterface()->getResource(window, QSGRendererInterface::CommandListResource);
    if (!commandList) return false;

    window->beginExternalCommands();
    try
    {
        record(static_cast<MTL::CommandBuffer*>(commandList));
    }
    catch (...)
    {
        window->endExternalCommands();
        throw;
    }
    window->endExternalCommands();
    return true;
}

QSGTexture* QtGraphicsBridge::wrapTexture(QQuickWindow* window, MTL::Texture* texture, QSize size, bool hasAlpha)
{
    QQuickWindow::CreateTextureOptions options;
    if (hasAlpha) options |= QQuickWindow::TextureHasAlphaChannel;

    return QNativeInterface::QSGMetalTexture::fromNative(
        (__bridge id<MTLTexture>)static_cast<void*>(texture), window, size, options
    );
}

} // namespace helix::ui
