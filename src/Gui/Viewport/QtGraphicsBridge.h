#pragma once
#include "Graphics/Metal.h"
#include <QSize>
#include <functional>

class QQuickWindow;
class QSGTexture;

namespace helix::ui {

/// @brief The Metal specifics of the Qt Quick scene graph, which the viewport hands its frames to.
///
/// The scene graph runs on Metal through Qt's rendering hardware interface. The viewport draws
/// with the Metal device the scene graph itself uses, so a texture it makes can be sampled by the
/// scene graph with no copy.
///
/// Every function but `selectMetal` belongs to the render thread of the window.
class QtGraphicsBridge
{
  public:
    /// @brief Makes Qt Quick draw with Metal.
    ///
    /// @pre No window has been created yet.
    static void selectMetal();

    /// @brief Returns whether the scene graph of the window runs on Metal.
    static bool usesMetal(QQuickWindow* window);

    /// @brief Returns the Metal device of the scene graph, or null when it does not run on Metal.
    static MTL::Device* getDevice(QQuickWindow* window);

    /// @brief Records commands into the command buffer the scene graph is recording the frame into.
    ///
    /// Qt closes and reopens its own state around the call. The commands run on the queue of the
    /// scene graph, ahead of the passes it records afterwards, so Metal orders them before any
    /// sampling of the texture they draw into.
    ///
    /// @pre The scene graph is between its start of the frame and its first render pass, which is
    /// where `QQuickWindow::beforeRendering` is emitted.
    /// @return whether the scene graph had a command buffer to record into.
    static bool recordCommands(QQuickWindow* window, const std::function<void(MTL::CommandBuffer*)>& record);

    /// @brief Returns a scene graph texture showing the Metal texture, owned by the caller.
    ///
    /// The scene graph does not retain the Metal texture, so the caller keeps it alive for as long
    /// as the scene graph texture, and a few frames after.
    ///
    /// @param hasAlpha whether the texture is blended by its alpha.
    static QSGTexture* wrapTexture(QQuickWindow* window, MTL::Texture* texture, QSize size, bool hasAlpha);
};

} // namespace helix::ui
