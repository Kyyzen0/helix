#include "Graphics/ViewportRenderer.h"
#include "Graphics/RenderTargetFormats.h"
#include "Graphics/Shaders/ShaderTypes.h"

#include <algorithm>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>
#include <stdexcept>
#include <string>

namespace helix::gfx {

namespace {

/// @brief How many frames a replaced set of targets is kept for.
///
/// A host records a few frames ahead of the GPU, and the scene graph of Qt Quick does not hold
/// a reference of its own on the texture it samples, so the old texture outlives the swap.
constexpr std::uint64_t retiredFrameCount = 4;

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
    return FrameConstants{
        viewProjection, cameraPosition, frame.geometryColor, viewportPixelSize, frame.pixelRatio, 0.f
    };
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
      cameraPrimPass_(device, sampleCount_)
{
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
        if (targets_.color) retiredTargets_.push_back({std::move(targets_), frameIndex_});
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
    meshPass_.upload(device, geometry);
    pointPass_.upload(device, geometry);
    cameraPrimPass_.upload(device, geometry);
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
    const FrameConstants frameConstants = getFrameConstants(frame);

    ++frameIndex_;
    while (!retiredTargets_.empty() && frameIndex_ - retiredTargets_.front().retiredAtFrame > retiredFrameCount)
        retiredTargets_.pop_front();

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
    meshPass_.draw(encoder, frame.wireframeVisible);
    pointPass_.draw(encoder, frame.pointsVisible);
    cameraPrimPass_.draw(encoder, frame.viewCameraIndex);
    encoder->endEncoding();
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
