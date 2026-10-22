#include "Graphics/GraphicsDevice.h"
#include "Graphics/VulkanCapabilities.h"
#include "Graphics/VulkanLoader.h"
#include "GraphicsTestUtils.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <stdexcept>

using namespace enzo::gfx;

TEST_CASE("A graphics device hands out the handles a host adopts")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    VulkanHandles vulkanHandles = device->getVulkanHandles();

    REQUIRE(vulkanHandles.instance != nullptr);
    REQUIRE(vulkanHandles.physicalDevice != nullptr);
    REQUIRE(vulkanHandles.device != nullptr);
    REQUIRE(vulkanHandles.queue != nullptr);

    // Packs version 1.3 the way VK_MAKE_API_VERSION does.
    const uint32_t vulkan13 = (1u << 22) | (3u << 12);
    REQUIRE(vulkanHandles.apiVersion >= vulkan13);
}

TEST_CASE("Only one graphics device exists per process")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();

    REQUIRE(&GraphicsDevice::get() == device.get());
    REQUIRE_THROWS_AS(GraphicsDevice(), std::runtime_error);

    // Checks a new device can be made once the first is destroyed.
    device.reset();
    REQUIRE_THROWS_AS(GraphicsDevice::get(), std::runtime_error);
    device = createDeviceOrSkip();
    REQUIRE(&GraphicsDevice::get() == device.get());
}

TEST_CASE("A graphics device names the driver it runs on")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    const GraphicsDeviceFeatures& features = device->getFeatures();

    // The driver of a device is always known, since the properties of the device carry it.
    REQUIRE(!features.driverName.empty());
    REQUIRE(!features.adapterDescription.empty());

#if defined(__APPLE__)
    // macOS has no Vulkan driver of its own, so the device runs on MoltenVK, which is a
    // portability driver.
    REQUIRE(isMoltenVkDriver(features.driverName));
    REQUIRE(features.portabilityDriver);
#endif
}

TEST_CASE("A graphics device runs a version the viewport can draw with")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    const GraphicsDeviceFeatures& features = device->getFeatures();

    // The viewport draws with Vulkan 1.3, and the device never runs more than that.
    REQUIRE(features.apiVersion >= packVulkanVersion(1, 1));
    REQUIRE(features.apiVersion <= packVulkanVersion(1, 3));
}

TEST_CASE("A graphics device reports the loader it was opened with")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    const VulkanLoaderStatus& loader = device->getLoaderStatus();

    // A device can only be made with a loader, so the loader is found and its version is
    // known.
    REQUIRE(loader.loaded);
    REQUIRE(loader.library != nullptr);
    REQUIRE(loader.version >= packVulkanVersion(1, 0));

    // The engine opens the loader by its name, so the loader has to be reachable that way.
    REQUIRE(loader.byName);
}

TEST_CASE("The viewport asks for the version the loader offers, and no more")
{
    // A loader that offers more than the viewport needs is held at the version the
    // viewport draws with.
    REQUIRE(chooseVulkanVersion(packVulkanVersion(1, 4)) == packVulkanVersion(1, 3));
    REQUIRE(chooseVulkanVersion(packVulkanVersion(1, 3)) == packVulkanVersion(1, 3));

    // A loader that offers less is asked for what it has.
    REQUIRE(chooseVulkanVersion(packVulkanVersion(1, 2)) == packVulkanVersion(1, 2));
    REQUIRE(chooseVulkanVersion(packVulkanVersion(1, 1)) == packVulkanVersion(1, 1));

    // A loader that announces nothing is taken at its oldest.
    REQUIRE(chooseVulkanVersion(0) == packVulkanVersion(1, 0));
}

TEST_CASE("A packed Vulkan version reads back as it was packed")
{
    REQUIRE(describeVulkanVersion(packVulkanVersion(1, 3)) == "1.3");
    REQUIRE(describeVulkanVersion(packVulkanVersion(1, 4)) == "1.4");
    REQUIRE(describeVulkanVersion(0) == "0.0");

    REQUIRE(vulkanVersionMajor(packVulkanVersion(1, 3)) == 1);
    REQUIRE(vulkanVersionMinor(packVulkanVersion(1, 3)) == 3);
}

TEST_CASE("A portability driver is told apart from the others")
{
    REQUIRE(isPortabilityDriver("MoltenVK"));
    REQUIRE(isMoltenVkDriver("MoltenVK"));
    REQUIRE(isPortabilityDriver("MoltenVK 1.4.2"));

    REQUIRE_FALSE(isPortabilityDriver("NVIDIA GeForce RTX 3080"));
    REQUIRE_FALSE(isMoltenVkDriver("NVIDIA GeForce RTX 3080"));
    REQUIRE_FALSE(isPortabilityDriver(""));
}

TEST_CASE("A host without a Vulkan device is told what MoltenVK needs")
{
    VulkanLoaderStatus loader;
    loader.tried = {"libvulkan.dylib", "/usr/local/lib/libvulkan.dylib"};
    loader.error = "no such file";

    const std::string message = describeVulkanFailure(loader, "the driver offered no device");

    REQUIRE(message.find("No Vulkan device could be created") != std::string::npos);
    REQUIRE(message.find("the driver offered no device") != std::string::npos);
    REQUIRE(message.find("libvulkan.dylib") != std::string::npos);

#if defined(__APPLE__)
    // macOS has no Vulkan driver of its own, so the message names MoltenVK.
    REQUIRE(message.find("MoltenVK") != std::string::npos);
    REQUIRE(message.find("Metal") != std::string::npos);
#endif
}

TEST_CASE("A host whose loader lies elsewhere is told where the engine looks for it")
{
    VulkanLoaderStatus loader;
    loader.loaded = true;
    loader.byName = false;
    loader.path = "/opt/vulkan/libvulkan.dylib";

    const std::string message = describeVulkanFailure(loader, "the driver offered no device");

    // The engine opens the loader by its name, which the dynamic loader finds through the
    // run path of the program.
    REQUIRE(message.find("run path") != std::string::npos);
    REQUIRE(message.find("/opt/vulkan/libvulkan.dylib") != std::string::npos);
}
