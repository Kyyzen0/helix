#include "Graphics/Passes/PassSetup.h"
#include "Graphics/RenderTargetFormats.h"
#include <stdexcept>
#include <string>

namespace helix::gfx {

namespace {

std::string describeError(const NS::Error* error)
{
    if (!error || !error->localizedDescription()) return "unknown error";
    return error->localizedDescription()->utf8String();
}

NS::SharedPtr<MTL::DepthStencilState> createDepthState(
    MTL::Device* device, MTL::CompareFunction compare, bool write
)
{
    NS::SharedPtr<MTL::DepthStencilDescriptor> descriptor =
        NS::TransferPtr(MTL::DepthStencilDescriptor::alloc()->init());
    descriptor->setDepthCompareFunction(compare);
    descriptor->setDepthWriteEnabled(write);
    return NS::TransferPtr(device->newDepthStencilState(descriptor.get()));
}

NS::SharedPtr<MTL::Function> requireFunction(GraphicsDevice& device, const char* name)
{
    NS::SharedPtr<MTL::Function> function = device.getFunction(name);
    if (!function) throw std::runtime_error(std::string("The shader function ") + name + " was not found");
    return function;
}

} // namespace

Pipeline createPipeline(GraphicsDevice& device, const PipelineSettings& settings, NS::UInteger sampleCount)
{
    ScopedAutoreleasePool pool;

    NS::SharedPtr<MTL::RenderPipelineDescriptor> descriptor =
        NS::TransferPtr(MTL::RenderPipelineDescriptor::alloc()->init());
    descriptor->setLabel(toNsString(settings.name));

    NS::SharedPtr<MTL::Function> vertexFunction = requireFunction(device, settings.vertexFunction);
    descriptor->setVertexFunction(vertexFunction.get());
    NS::SharedPtr<MTL::Function> fragmentFunction;
    if (settings.fragmentFunction)
    {
        fragmentFunction = requireFunction(device, settings.fragmentFunction);
        descriptor->setFragmentFunction(fragmentFunction.get());
    }

    descriptor->setVertexDescriptor(settings.vertexLayout);
    descriptor->setInputPrimitiveTopology(settings.topology);
    descriptor->setRasterSampleCount(sampleCount);
    descriptor->setDepthAttachmentPixelFormat(depthFormat);

    MTL::RenderPipelineColorAttachmentDescriptor* color = descriptor->colorAttachments()->object(0);
    color->setPixelFormat(colorFormat);
    if (!settings.fragmentFunction)
    {
        // A depth prepass draws no colour, which leaves the colour of the frame to the
        // passes that follow it.
        color->setWriteMask(MTL::ColorWriteMaskNone);
    }
    else if (settings.blendOver)
    {
        // Blends the faded lines over the background and leaves the target opaque.
        color->setBlendingEnabled(true);
        color->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
        color->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
        color->setRgbBlendOperation(MTL::BlendOperationAdd);
        color->setWriteMask(MTL::ColorWriteMaskRed | MTL::ColorWriteMaskGreen | MTL::ColorWriteMaskBlue);
    }

    NS::Error* error = nullptr;
    Pipeline pipeline;
    pipeline.state = NS::TransferPtr(device.getDevice()->newRenderPipelineState(descriptor.get(), &error));
    if (!pipeline.state)
    {
        throw std::runtime_error(
            std::string("The pipeline ") + settings.name + " could not be built: " + describeError(error)
        );
    }
    pipeline.depthState = createDepthState(device.getDevice(), settings.depthCompare, settings.depthWrite);
    pipeline.depthBias = settings.depthBias;
    pipeline.slopeScaledDepthBias = settings.slopeScaledDepthBias;
    return pipeline;
}

void bindPipeline(MTL::RenderCommandEncoder* encoder, const Pipeline& pipeline)
{
    encoder->setRenderPipelineState(pipeline.state.get());
    encoder->setDepthStencilState(pipeline.depthState.get());
    encoder->setDepthBias(pipeline.depthBias, pipeline.slopeScaledDepthBias, 0.f);
}

void addVertexAttribute(
    MTL::VertexDescriptor* layout,
    NS::UInteger attribute,
    MTL::VertexFormat format,
    NS::UInteger offset,
    NS::UInteger bufferIndex
)
{
    MTL::VertexAttributeDescriptor* description = layout->attributes()->object(attribute);
    description->setFormat(format);
    description->setOffset(offset);
    description->setBufferIndex(bufferIndex);
}

void addVertexBuffer(MTL::VertexDescriptor* layout, NS::UInteger bufferIndex, NS::UInteger stride, bool perInstance)
{
    MTL::VertexBufferLayoutDescriptor* description = layout->layouts()->object(bufferIndex);
    description->setStride(stride);
    description->setStepFunction(perInstance ? MTL::VertexStepFunctionPerInstance : MTL::VertexStepFunctionPerVertex);
    description->setStepRate(1);
}

NS::SharedPtr<MTL::Buffer> createSharedBuffer(MTL::Device* device, const char* name, const void* bytes, std::size_t size)
{
    // The size is checked before the device is asked, since a request past the limit of the
    // device is refused with no reason.
    if (size > device->maxBufferLength())
    {
        throw std::runtime_error(
            std::string("The buffer ") + name + " is larger than the device allows (" + std::to_string(size) +
            " bytes)"
        );
    }

    // Shared buffers live in the memory both the CPU and the GPU see on Apple silicon, so the
    // bytes are copied once, by this call, and the GPU reads them where they are.
    NS::SharedPtr<MTL::Buffer> buffer = NS::TransferPtr(device->newBuffer(bytes, size, MTL::ResourceStorageModeShared));
    if (!buffer) throw std::runtime_error(std::string("The buffer ") + name + " could not be allocated");
    buffer->setLabel(toNsString(name));
    return buffer;
}

} // namespace helix::gfx
