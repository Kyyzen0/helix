// The macOS and MoltenVK specifics of this file.
//
// macOS has no Vulkan driver of its own. Every Vulkan device there comes from MoltenVK,
// which translates Vulkan onto Metal, and that changes the calls below in these ways:
//
// 1. The Vulkan loader
//    The engine opens the loader by its leaf name, which the dynamic loader finds through
//    the run path of the program, through DYLD_LIBRARY_PATH or in the system folders. A
//    macOS build tree keeps the loader in a folder of its own, so the loader has to be on
//    the run path of the program or in /usr/local/lib for the engine to find it.
//    prepareVulkanLoader() opens the loader the same way the engine opens it, so that the
//    version it offers and the name of the driver can be read, and so that a host that
//    keeps the loader elsewhere is told where the engine looks for it.
//
// 2. Diligent::IEngineFactoryVk::CreateDeviceAndContextsVk
//    The engine holds back the errors it reports rather than throwing them, so a host
//    that wants to name the reason of a missing device reads it from the last message.
//    The messages below name MoltenVK and say what to install.
//
// 3. Diligent::EngineVkCreateInfo::GraphicsAPIVersion
//    The version asked of the driver is the smaller of the one the loader offers and
//    Vulkan 1.3, which is what the viewport draws with. A desktop driver usually offers
//    more, and asking for it would let the engine take paths that MoltenVK does not
//    reach. A loader that announces nothing is taken at Vulkan 1.0, which the
//    extensions carry. MoltenVK 1.4.2 offers Vulkan 1.4.357, and the instance it makes
//    for this program is a 1.3 one.
//
// 4. The instance extensions
//    Diligent adds VK_KHR_portability_enumeration on macOS and sets
//    VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR on the instance, which the Vulkan
//    loader of 1.3.216 and later insists on before it lists a portability driver. The
//    loader keeps that extension to itself and hands the driver the rest. Diligent's
//    build defines VK_USE_PLATFORM_METAL_EXT on Apple, so it adds VK_EXT_metal_surface as
//    well, which is what a surface of MoltenVK needs. The viewport draws into an image
//    that the host samples rather than into a swap chain, so no surface is made here.
//
// 5. The device extensions
//    Diligent asks for VK_KHR_swapchain whenever the instance carries VK_KHR_surface.
//    MoltenVK has no swap chain of its own, but it answers the extension, so the device
//    is created either way.
//
// 6. The device features
//    Diligent asks only for the features the device reports, so the smaller feature set
//    of MoltenVK needs nothing here.
//
// 7. The driver properties
//    The name of the driver is only in the properties of the Vulkan device, which the
//    engine does not hand out. The loader is asked for the calls that read them, and
//    only when the device holds VK_KHR_driver_properties, which MoltenVK does.

#include "Graphics/GraphicsDevice.h"
#include "Graphics/DiligentObjects.h"
#include "Graphics/VulkanCapabilities.h"
#include "Graphics/VulkanLoader.h"
#include <cstring>
#include <iostream>
#include <stdexcept>

#include <CommandQueueVk.h>
#include <EngineFactoryVk.h>
#include <RenderDeviceVk.h>

namespace enzo::gfx {

namespace {

GraphicsDevice* processDevice = nullptr;

/// @brief The last error the engine reported, kept to name it in the message of the host.
std::string lastEngineError;

/// @brief Prints Diligent's warnings and errors, and drops its info messages.
void printDiligentMessage(
    Diligent::DEBUG_MESSAGE_SEVERITY severity,
    const Diligent::Char* message,
    const Diligent::Char*,
    const Diligent::Char*,
    int
)
{
    if (severity == Diligent::DEBUG_MESSAGE_SEVERITY_INFO) return;
    if (message) lastEngineError = message;
    std::cerr << "graphics device: " << message << "\n";
}

/// @brief Returns the entry point of the loader for a call of the instance.
template <typename Function>
Function getInstanceFunction(void* library, VkInstance instance, const char* name)
{
    const auto getInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(detail::findSymbol(library, "vkGetInstanceProcAddr"));
    if (!getInstanceProcAddr) return nullptr;
    return reinterpret_cast<Function>(getInstanceProcAddr(instance, name));
}

/// @brief Returns the name the driver of the device gives itself.
///
/// The name is only in the driver properties, which the engine does not hand out, so the
/// loader is asked for the calls that read them.
std::string readDriverName(
    const VulkanLoaderStatus& loader, Diligent::IRenderDevice* device, std::string& adapterDescription
)
{
    adapterDescription = device->GetAdapterInfo().Description;

    if (!loader.library) return {};

    Diligent::RefCntAutoPtr<Diligent::IRenderDeviceVk> vulkanDevice{
        device, Diligent::IID_RenderDeviceVk
    };
    if (!vulkanDevice) return {};

    const VkInstance instance = vulkanDevice->GetVkInstance();
    const VkPhysicalDevice physicalDevice = vulkanDevice->GetVkPhysicalDevice();

    // The driver properties are only read when the device holds the extension that carries
    // them, since the driver would not know the structure otherwise.
    const auto enumerateExtensions = getInstanceFunction<PFN_vkEnumerateDeviceExtensionProperties>(
        loader.library, instance, "vkEnumerateDeviceExtensionProperties"
    );
    if (!enumerateExtensions) return {};

    uint32_t extensionCount = 0;
    if (enumerateExtensions(physicalDevice, nullptr, &extensionCount, nullptr) != VK_SUCCESS) return {};
    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (enumerateExtensions(physicalDevice, nullptr, &extensionCount, extensions.data()) != VK_SUCCESS)
    {
        return {};
    }

    bool hasDriverProperties = false;
    for (const VkExtensionProperties& extension : extensions)
    {
        if (std::strcmp(extension.extensionName, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME) == 0)
        {
            hasDriverProperties = true;
            break;
        }
    }
    if (!hasDriverProperties) return {};

    auto getProperties2 = getInstanceFunction<PFN_vkGetPhysicalDeviceProperties2>(
        loader.library, instance, "vkGetPhysicalDeviceProperties2"
    );
    if (!getProperties2)
    {
        getProperties2 = getInstanceFunction<PFN_vkGetPhysicalDeviceProperties2>(
            loader.library, instance, "vkGetPhysicalDeviceProperties2KHR"
        );
    }
    if (!getProperties2) return {};

    VkPhysicalDeviceDriverProperties driverProperties{};
    driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &driverProperties;
    getProperties2(physicalDevice, &properties);
    return std::string(driverProperties.driverName);
}

} // namespace

GraphicsDevice::GraphicsDevice(const std::vector<std::string>& instanceExtensions)
{
    if (processDevice)
    {
        throw std::runtime_error("A graphics device already exists in this process");
    }

    // Opens the loader the way the engine opens it, so that the version it offers and the
    // name of the driver can be read, and so that a host that keeps the loader elsewhere
    // says so.
    loaderStatus_ = prepareVulkanLoader();
    if (!loaderStatus_.loaded)
    {
        throw std::runtime_error(describeVulkanFailure(loaderStatus_, "the Vulkan loader could not be opened"));
    }

    std::vector<const char*> instanceExtensionNames;
    for (const std::string& extension : instanceExtensions)
    {
        instanceExtensionNames.push_back(extension.c_str());
    }

    Diligent::EngineVkCreateInfo createInfo;
    const uint32_t version = chooseVulkanVersion(loaderStatus_.version);
    createInfo.GraphicsAPIVersion = {vulkanVersionMajor(version), vulkanVersionMinor(version)};
    createInfo.InstanceExtensionCount = static_cast<Diligent::Uint32>(instanceExtensionNames.size());
    createInfo.ppInstanceExtensionNames = instanceExtensionNames.data();

    diligent_ = std::make_unique<DiligentObjects>();
    Diligent::IEngineFactoryVk* factory = Diligent::GetEngineFactoryVk();
    factory->SetMessageCallback(printDiligentMessage);
    lastEngineError.clear();
    try
    {
        factory->CreateDeviceAndContextsVk(createInfo, &diligent_->device, &diligent_->context);
    }
    catch (const std::runtime_error& error)
    {
        // The engine reports a driver that is missing or a device that cannot be made in
        // a way that names neither the driver nor what to do about it.
        throw std::runtime_error(describeVulkanFailure(loaderStatus_, error.what()));
    }
    if (!diligent_->device)
    {
        // The engine holds back its errors rather than throwing them, so the last one it
        // reported is the reason of the failure.
        const std::string reason = lastEngineError.empty() ? "the driver offered no device"
                                                           : lastEngineError;
        throw std::runtime_error(describeVulkanFailure(loaderStatus_, reason));
    }

    features_.driverName = readDriverName(loaderStatus_, diligent_->device, features_.adapterDescription);
    features_.portabilityDriver = isPortabilityDriver(features_.driverName);

    // The device runs the smaller of the version of the instance and the one the driver
    // offers, which is the version the viewport draws with.
    Diligent::RefCntAutoPtr<Diligent::IRenderDeviceVk> vulkanDevice{
        diligent_->device, Diligent::IID_RenderDeviceVk
    };
    if (vulkanDevice) features_.apiVersion = vulkanDevice->GetVkVersion();

    processDevice = this;
}

GraphicsDevice::~GraphicsDevice()
{
    diligent_->context->Flush();
    diligent_->device->IdleGPU();

    processDevice = nullptr;
}

GraphicsDevice& GraphicsDevice::get()
{
    if (!processDevice)
    {
        throw std::runtime_error("No graphics device exists in this process");
    }
    return *processDevice;
}

const std::string& GraphicsDevice::getLastEngineError() { return lastEngineError; }

void GraphicsDevice::clearLastEngineError() { lastEngineError.clear(); }

GraphicsDevice::DiligentObjects& GraphicsDevice::getDiligentObjects() const { return *diligent_; }

VulkanHandles GraphicsDevice::getVulkanHandles() const
{
    Diligent::RefCntAutoPtr<Diligent::IRenderDeviceVk> vulkanDevice{
        diligent_->device, Diligent::IID_RenderDeviceVk
    };

    // Takes the queue from the context, since every context shares the one queue Diligent creates.
    Diligent::ICommandQueue* queue = diligent_->context->LockCommandQueue();
    Diligent::RefCntAutoPtr<Diligent::ICommandQueueVk> vulkanQueue{
        queue, Diligent::IID_CommandQueueVk
    };
    VulkanHandles vulkanHandles;
    vulkanHandles.queue = vulkanQueue->GetVkQueue();
    vulkanHandles.queueFamilyIndex = vulkanQueue->GetQueueFamilyIndex();
    diligent_->context->UnlockCommandQueue();

    vulkanHandles.instance = vulkanDevice->GetVkInstance();
    vulkanHandles.physicalDevice = vulkanDevice->GetVkPhysicalDevice();
    vulkanHandles.device = vulkanDevice->GetVkDevice();
    vulkanHandles.apiVersion = vulkanDevice->GetVkVersion();
    return vulkanHandles;
}

} // namespace enzo::gfx
