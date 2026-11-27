// The viewport half of the facade: the camera, the geometry the viewport draws and the Metal
// frame. See the Viewport section of HelixCore.h for who owns what.

#include "Engine/Network/NodePacket.h"
#include "Graphics/DisplayGeometry.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/ViewportCamera.h"
#include "Graphics/ViewportRenderer.h"
#include "HelixCore/HelixCore.h"
#include "HelixCore/HelixCoreInternal.h"
#include "HelixCore/ImageFile.h"
#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/vec3.hpp>
#include <limits>
#include <memory>
#include <string>

namespace helix::core {

using detail::failure;
using detail::guarded;
using detail::success;

namespace {

// Like the rest of the facade, this is used from one thread only.
struct ViewportState
{
    // The renderer holds a reference on the device, so it is declared after it and goes first.
    std::unique_ptr<gfx::GraphicsDevice> device;
    std::unique_ptr<gfx::ViewportRenderer> renderer;
    gfx::ViewportCamera camera;
    std::shared_ptr<const gfx::DisplayGeometry> geometry;
    Bounds bounds;
};

ViewportState& viewport()
{
    static ViewportState state;
    return state;
}

// The renderer of renderToImage, on the default device, made when first needed.
struct OffscreenState
{
    std::unique_ptr<gfx::GraphicsDevice> device;
    std::unique_ptr<gfx::ViewportRenderer> renderer;
};

OffscreenState& offscreen()
{
    static OffscreenState state;
    return state;
}

Bounds computeBounds(const gfx::DisplayGeometry& geometry)
{
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    bool any = false;
    for (const std::vector<glm::vec3>* points : {&geometry.positions, &geometry.pointPositions})
    {
        for (const glm::vec3& point : *points)
        {
            if (glm::any(glm::isnan(point)) || glm::any(glm::isinf(point))) continue;
            minimum = glm::min(minimum, point);
            maximum = glm::max(maximum, point);
            any = true;
        }
    }

    Bounds bounds;
    if (!any) return bounds;
    bounds.valid = true;
    bounds.minX = minimum.x;
    bounds.minY = minimum.y;
    bounds.minZ = minimum.z;
    bounds.maxX = maximum.x;
    bounds.maxY = maximum.y;
    bounds.maxZ = maximum.z;
    return bounds;
}

// Cooks the display node and flattens it for the renderer.
Status
cookGeometry(std::shared_ptr<const gfx::DisplayGeometry>& geometry, ViewportGeometryInfo& info)
{
    std::shared_ptr<const NodePacket> packet;
    if (Status status = detail::cookDisplayPacket(packet); !status.ok) return status;

    for (const auto& primitive : packet->getPrimitives())
    {
        if (primitive) info.pointCount += primitive->getNumPoints();
    }

    auto built = std::make_shared<gfx::DisplayGeometry>();
    gfx::buildDisplayGeometry(*built, *packet);
    info.triangleCount = built->topology.triangleIndices.size() / 3;
    info.bounds = computeBounds(*built);
    geometry = std::move(built);
    return success();
}

gfx::FrameState makeFrame(const ViewportFrameOptions& options, const gfx::ViewportCamera& camera)
{
    gfx::FrameState frame;
    frame.pixelSize = {options.pixelWidth, options.pixelHeight};
    frame.pixelRatio = options.pixelRatio;
    frame.backgroundColor = {
        options.backgroundRed,
        options.backgroundGreen,
        options.backgroundBlue,
        options.backgroundAlpha
    };
    frame.geometryColor = {options.geometryRed, options.geometryGreen, options.geometryBlue, 1.f};
    frame.gridColor = {options.gridRed, options.gridGreen, options.gridBlue, 1.f};
    frame.wireframeVisible = options.wireframe != WireframeMode::Off;
    frame.wireframeAdaptive = options.wireframe == WireframeMode::Auto;
    frame.pointsVisible = options.pointsVisible;
    frame.materialsVisible = options.materialsVisible;
    frame.camera = camera;
    return frame;
}

} // namespace

Status startViewport(void* metalDevice)
{
    stopViewport();
    return guarded([&] {
        // The graphics device retains the Metal device itself, so the caller's reference is
        // only borrowed for this call.
        auto device = std::make_unique<gfx::GraphicsDevice>(static_cast<MTL::Device*>(metalDevice));
        auto renderer = std::make_unique<gfx::ViewportRenderer>(*device);
        viewport().device = std::move(device);
        viewport().renderer = std::move(renderer);
        return success();
    });
}

void stopViewport()
{
    viewport().renderer.reset();
    viewport().device.reset();
}

bool isViewportRunning() { return viewport().renderer != nullptr; }

ViewportGeometryInfo updateViewportGeometry()
{
    ViewportGeometryInfo info;
    ViewportState& state = viewport();
    std::shared_ptr<const gfx::DisplayGeometry> geometry;
    info.status = guarded([&] { return cookGeometry(geometry, info); });

    // A failed cook leaves the viewport with no geometry, not with a stale one.
    if (!info.status.ok)
    {
        info = ViewportGeometryInfo{info.status, 0, 0, {}};
        geometry.reset();
    }
    state.geometry = std::move(geometry);
    state.bounds = info.bounds;
    return info;
}

void orbitViewport(float deltaX, float deltaY) { viewport().camera.orbit({deltaX, deltaY}); }

void panViewport(float deltaX, float deltaY) { viewport().camera.pan({deltaX, deltaY}); }

void dollyViewport(float amount) { viewport().camera.dolly(amount); }

void orbitViewportTo(float azimuthDegrees, float elevationDegrees)
{
    if (!std::isfinite(azimuthDegrees) || !std::isfinite(elevationDegrees)) return;
    viewport().camera.setAngles(glm::radians(azimuthDegrees), glm::radians(elevationDegrees));
}

void resetViewport() { viewport().camera.reset(); }

bool frameViewport(float aspect)
{
    ViewportState& state = viewport();
    if (!state.bounds.valid) return false;
    const Bounds& bounds = state.bounds;
    state.camera.frameBounds(
        {bounds.minX, bounds.minY, bounds.minZ},
        {bounds.maxX, bounds.maxY, bounds.maxZ},
        aspect
    );
    return true;
}

ViewportFrame encodeViewport(void* commandBuffer, const ViewportFrameOptions& options)
{
    ViewportFrame result;
    result.status = guarded([&] {
        ViewportState& state = viewport();
        if (!state.renderer) return failure("The viewport is not started.");
        if (!commandBuffer) return failure("No command buffer was given.");

        gfx::FrameState frame = makeFrame(options, state.camera);
        frame.geometry = state.geometry;
        const gfx::RenderedImage image =
            state.renderer->encode(frame, static_cast<MTL::CommandBuffer*>(commandBuffer));
        result.colorTexture = image.texture;
        result.pixelWidth = image.size.x;
        result.pixelHeight = image.size.y;
        return success();
    });
    return result;
}

Status
renderToImage(const std::string& path, const ViewportFrameOptions& options, bool useViewportCamera)
{
    return guarded([&] {
        if (!isInitialized()) return failure("The engine is not initialized.");
        if (path.empty()) return failure("No file name was given.");
        if (options.pixelWidth == 0 || options.pixelHeight == 0)
            return failure("The image needs a width and a height.");

        std::shared_ptr<const gfx::DisplayGeometry> geometry;
        ViewportGeometryInfo info;
        if (Status status = cookGeometry(geometry, info); !status.ok) return status;

        gfx::ViewportCamera camera = useViewportCamera ? viewport().camera : gfx::ViewportCamera();
        if (!useViewportCamera && info.bounds.valid)
        {
            const Bounds& bounds = info.bounds;
            camera.frameBounds(
                {bounds.minX, bounds.minY, bounds.minZ},
                {bounds.maxX, bounds.maxY, bounds.maxZ},
                float(options.pixelWidth) / float(options.pixelHeight)
            );
        }

        OffscreenState& state = offscreen();
        if (!state.renderer)
        {
            state.device = std::make_unique<gfx::GraphicsDevice>();
            state.renderer = std::make_unique<gfx::ViewportRenderer>(*state.device);
        }

        gfx::FrameState frame = makeFrame(options, camera);
        frame.geometry = geometry;
        const gfx::PresentedFrame presented = state.renderer->renderToPixels(frame);
        if (presented.pixels.empty()) return failure("Nothing was drawn.");

        const std::string error =
            detail::writePng(path, presented.size.x, presented.size.y, presented.pixels);
        return error.empty() ? success() : failure(error);
    });
}

} // namespace helix::core
