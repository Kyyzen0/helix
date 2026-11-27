#include "Graphics/ViewportRenderer.h"
#include "Graphics/RenderTargetFormats.h"
#include "Graphics/Shaders/ShaderTypes.h"

#include <algorithm>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>
#include <stdexcept>
#include <string>

namespace helix::gfx {

namespace {

FrameConstants getFrameConstants(const FrameState& frame)
{
    glm::mat4 view = frame.camera.getViewMatrix();
    glm::vec4 cameraPosition{frame.camera.getPosition(), 1.f};

    // A view camera the geometry does not hold is looked through as the orbit camera.
    if (frame.viewCameraIndex.has_value() && frame.geometry &&
        *frame.viewCameraIndex < frame.geometry->cameraTransforms.size())
    {
        const glm::mat4& viewCameraTransform = frame.geometry->cameraTransforms[*frame.viewCameraIndex];
        view = glm::inverse(viewCameraTransform);
        cameraPosition = viewCameraTransform[3];
    }

    const float aspect = float(frame.pixelSize.x) / float(std::max(frame.pixelSize.y, 1u));
    const glm::mat4 viewProjection = frame.camera.getProjectionMatrix(aspect) * view;
    const glm::vec2 viewportPixelSize{frame.pixelSize};

    // The rows of the rotation of the view matrix are the axes of the camera in world space.
    const glm::vec4 cameraRight{view[0][0], view[1][0], view[2][0], 0.f};
    const glm::vec4 cameraUp{view[0][1], view[1][1], view[2][1], 0.f};
    return FrameConstants{
        viewProjection,
        cameraPosition,
        frame.geometryColor,
        cameraRight,
        cameraUp,
        viewportPixelSize,
        frame.pixelRatio,
        1.f
    };
}

// Returns how opaque the wireframe is for this frame, and 0 when it is left out.
//
// A wireframe that is not adaptive is opaque. An adaptive one is left out above the triangle
// limit, and below it fades with the mean length of the edges on screen, so it disappears as the
// lines come to touch (a dense mesh, or a view from far away) and comes back when they part.
float getWireframeOpacity(const FrameState& frame, const MeshPass& mesh, const FrameConstants& constants)
{
    if (!frame.wireframeVisible) return 0.f;
    if (!frame.wireframeAdaptive) return 1.f;
    // The edges would draw over the picture of a textured mesh, which they do not help to read.
    if (frame.materialsVisible && mesh.hasTexturedMaterial()) return 0.f;
    if (mesh.triangleCount() > kWireframeAutoTriangleLimit) return 0.f;

    // The size of an edge at the distance of the middle of the mesh, in device pixels: the
    // vertical field of view spans the height of the frame.
    const float distance = std::max(glm::length(glm::vec3(constants.cameraPosition) - mesh.center()), 1e-4f);
    const float aspect = float(frame.pixelSize.x) / float(std::max(frame.pixelSize.y, 1u));
    const float cotHalfFov = frame.camera.getProjectionMatrix(aspect)[1][1];
    const float pixelsPerUnit = 0.5f * float(frame.pixelSize.y) * cotHalfFov / distance;
    const float edgePoints = mesh.meanEdgeLength() * pixelsPerUnit / std::max(frame.pixelRatio, 1e-3f);
    return adaptiveWireframeOpacity(edgePoints);
}

std::string describeError(const NS::Error* error)
{
    if (!error || !error->localizedDescription()) return "unknown error";
    return error->localizedDescription()->utf8String();
}

} // namespace

ViewportRenderer::ViewportRenderer(GraphicsDevice& device)
    // A device that does not offer multisampled textures draws into a single sample, and
    // the pipelines have to be built for the sample count the targets hold.
    : device_(device),
      sampleCount_(chooseSampleCount(device.getDevice())),
      gridPass_(device, sampleCount_),
      meshPass_(device, sampleCount_),
      pointPass_(device, sampleCount_),
      cameraPrimPass_(device, sampleCount_),
      inFlightCount_(std::make_shared<std::atomic<std::size_t>>(0))
{
}

std::size_t ViewportRenderer::retiredResourceCount() const
{
    return retired_.size() + inFlightCount_->load();
}

ViewportRenderer::~ViewportRenderer() = default;

NS::SharedPtr<MTL::Texture> ViewportRenderer::createTexture(
    glm::uvec2 size,
    MTL::PixelFormat format,
    NS::UInteger samples,
    MTL::StorageMode storage,
    MTL::TextureUsage usage,
    const char* name
) const
{
    NS::SharedPtr<MTL::TextureDescriptor> descriptor = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
    descriptor->setTextureType(samples > 1 ? MTL::TextureType2DMultisample : MTL::TextureType2D);
    descriptor->setPixelFormat(format);
    descriptor->setWidth(size.x);
    descriptor->setHeight(size.y);
    descriptor->setSampleCount(samples);
    descriptor->setStorageMode(storage);
    descriptor->setUsage(usage);

    NS::SharedPtr<MTL::Texture> texture = NS::TransferPtr(device_.getDevice()->newTexture(descriptor.get()));
    if (!texture)
    {
        throw std::runtime_error(
            std::string("The viewport target ") + name + " could not be allocated at " + std::to_string(size.x) +
            "x" + std::to_string(size.y)
        );
    }
    texture->setLabel(toNsString(name));
    return texture;
}

ViewportRenderer::Targets ViewportRenderer::createTargets(glm::uvec2 size) const
{
    // The multisampled textures only ever live in the tile memory of an Apple GPU: the depth is
    // dropped when the pass ends and the colour is resolved into the shown texture. Such a
    // texture is memoryless, which takes no memory at all.
    const MTL::StorageMode tileStorage =
        device_.getFeatures().appleGpu ? MTL::StorageModeMemoryless : MTL::StorageModePrivate;

    Targets targets;
    targets.size = size;
    if (sampleCount_ > 1)
    {
        targets.multisample = createTexture(
            size, colorFormat, sampleCount_, tileStorage, MTL::TextureUsageRenderTarget, "Viewport multisample colour"
        );
    }
    targets.depth =
        createTexture(size, depthFormat, sampleCount_, tileStorage, MTL::TextureUsageRenderTarget, "Viewport depth");

    // The shown texture is shared, so that the CPU reads its pixels back where the GPU wrote them.
    targets.color = createTexture(
        size,
        colorFormat,
        1,
        MTL::StorageModeShared,
        MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead,
        "Viewport colour"
    );
    return targets;
}

RenderedImage ViewportRenderer::prepareTarget(glm::uvec2 size)
{
    if (size.x == 0 || size.y == 0) return {};

    if (!targets_.color || targets_.size != size)
    {
        ScopedAutoreleasePool pool;
        Targets fresh = createTargets(size);
        // The texture the host samples may still be read by a frame in flight.
        for (const NS::SharedPtr<MTL::Texture>* texture : {&targets_.multisample, &targets_.depth, &targets_.color})
            if (*texture) retired_.push_back(*texture);
        targets_ = std::move(fresh);
    }
    return {targets_.color.get(), targets_.size};
}

void ViewportRenderer::uploadGeometry(const FrameState& frame)
{
    if (frame.geometry == uploadedGeometry_) return;

    const DisplayGeometry noGeometry;
    const DisplayGeometry& geometry = frame.geometry ? *frame.geometry : noGeometry;
    MTL::Device* device = device_.getDevice();
    meshPass_.upload(device, geometry, retired_);
    pointPass_.upload(device, geometry, retired_);
    cameraPrimPass_.upload(device, geometry, retired_);
    uploadedGeometry_ = frame.geometry;
}

RenderedImage ViewportRenderer::encode(const FrameState& frame, MTL::CommandBuffer* commandBuffer)
{
    // A frame with no pixels has nothing to draw into.
    if (frame.pixelSize.x == 0 || frame.pixelSize.y == 0) return {};

    // Every object made below that is not kept is released with the pool.
    ScopedAutoreleasePool pool;

    // The work that can fail runs before an encoder is opened, so a failure never leaves one open.
    const RenderedImage image = prepareTarget(frame.pixelSize);
    uploadGeometry(frame);
    meshPass_.prepareTextures(device_.getDevice(), commandBuffer, frame.materialsVisible);
    FrameConstants frameConstants = getFrameConstants(frame);
    frameConstants.wireframeOpacity = getWireframeOpacity(frame, meshPass_, frameConstants);

    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();

    // The passes draw into the multisampled colour, which is resolved into the shown texture as
    // the pass ends.
    MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
    if (targets_.multisample)
    {
        color->setTexture(targets_.multisample.get());
        color->setResolveTexture(targets_.color.get());
        color->setStoreAction(MTL::StoreActionMultisampleResolve);
    }
    else
    {
        color->setTexture(targets_.color.get());
        color->setStoreAction(MTL::StoreActionStore);
    }
    color->setLoadAction(MTL::LoadActionClear);
    color->setClearColor(
        MTL::ClearColor::Make(
            frame.backgroundColor.r, frame.backgroundColor.g, frame.backgroundColor.b, frame.backgroundColor.a
        )
    );

    MTL::RenderPassDepthAttachmentDescriptor* depth = pass->depthAttachment();
    depth->setTexture(targets_.depth.get());
    depth->setLoadAction(MTL::LoadActionClear);
    depth->setClearDepth(1.0);
    depth->setStoreAction(MTL::StoreActionDontCare);

    MTL::RenderCommandEncoder* encoder = commandBuffer->renderCommandEncoder(pass);
    if (!encoder) throw std::runtime_error("The viewport could not be drawn: no render encoder was available");
    encoder->setLabel(toNsString("Viewport"));

    // The constants fit in the bytes a command carries, so no buffer has to outlive the frame.
    encoder->setVertexBytes(&frameConstants, sizeof(frameConstants), HELIX_FRAME_CONSTANTS_SLOT);
    encoder->setFragmentBytes(&frameConstants, sizeof(frameConstants), HELIX_FRAME_CONSTANTS_SLOT);

    gridPass_.draw(encoder);
    meshPass_.draw(encoder, frameConstants.wireframeOpacity > 0.f, frame.materialsVisible);
    pointPass_.draw(encoder, frame.pointsVisible);
    cameraPrimPass_.draw(encoder, frame.viewCameraIndex);
    encoder->endEncoding();

    // The command buffer of a host holds no reference on what it reads (Qt creates its buffers
    // with unretained references), so what was replaced since the last frame stays alive until
    // this one has finished: every frame recorded before it is ordered before it on the queue.
    // The handler runs on another thread and touches nothing of the renderer.
    if (!retired_.empty())
    {
        auto held = std::make_shared<RetiredResources>(std::move(retired_));
        retired_.clear();
        const std::size_t count = held->size();
        std::shared_ptr<std::atomic<std::size_t>> inFlight = inFlightCount_;
        inFlight->fetch_add(count);
        commandBuffer->addCompletedHandler(
            [held = std::move(held), inFlight, count](MTL::CommandBuffer*)
            {
                held->clear();
                inFlight->fetch_sub(count);
            }
        );
    }
    return image;
}

RenderedImage ViewportRenderer::render(const FrameState& frame)
{
    if (frame.pixelSize.x == 0 || frame.pixelSize.y == 0) return {};

    ScopedAutoreleasePool pool;
    MTL::CommandBuffer* commandBuffer = device_.getCommandQueue()->commandBuffer();
    commandBuffer->setLabel(toNsString("Viewport frame"));

    RenderedImage image;
    try
    {
        image = encode(frame, commandBuffer);
    }
    catch (const std::exception& error)
    {
        throw std::runtime_error(std::string("The viewport could not be drawn: ") + error.what());
    }

    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();
    if (commandBuffer->status() == MTL::CommandBufferStatusError)
    {
        throw std::runtime_error(
            std::string("The viewport could not be drawn: ") + describeError(commandBuffer->error())
        );
    }
    return image;
}

PresentedFrame ViewportRenderer::renderToPixels(const FrameState& frame)
{
    PresentedFrame presented;
    presented.image = render(frame);
    presented.size = frame.pixelSize;
    presented.pixelRatio = frame.pixelRatio;
    if (!presented.image.texture) return presented;

    // The frame is complete and the texture is shared, so the CPU reads it where it lies.
    const std::size_t rowBytes = std::size_t(frame.pixelSize.x) * 4;
    presented.pixels.resize(rowBytes * frame.pixelSize.y);
    presented.image.texture->getBytes(
        presented.pixels.data(),
        rowBytes,
        MTL::Region::Make2D(0, 0, frame.pixelSize.x, frame.pixelSize.y),
        0
    );
    return presented;
}

} // namespace helix::gfx
