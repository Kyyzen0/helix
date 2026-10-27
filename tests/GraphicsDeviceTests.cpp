#include "Graphics/GraphicsDevice.h"
#include "GraphicsTestUtils.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <stdexcept>
#include <string>

using namespace helix::gfx;

TEST_CASE("A graphics device wraps a Metal device and a command queue")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();

    REQUIRE(device->getDevice() != nullptr);
    REQUIRE(device->getCommandQueue() != nullptr);

    // The queue is made once and kept.
    REQUIRE(device->getCommandQueue() == device->getCommandQueue());
}

TEST_CASE("A graphics device names the GPU and what it can do")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    const GraphicsDeviceFeatures& features = device->getFeatures();

    REQUIRE(!features.deviceName.empty());

    // Helix runs on Apple silicon only, where the CPU and the GPU share their memory.
    REQUIRE(features.unifiedMemory);
    REQUIRE(features.appleGpu);
}

TEST_CASE("A graphics device adopts the Metal device of its host")
{
    std::unique_ptr<GraphicsDevice> first = createDeviceOrSkip();

    // The scene graph of Qt Quick owns the device, and the graphics device draws with it.
    GraphicsDevice adopted(first->getDevice());
    REQUIRE(adopted.getDevice() == first->getDevice());
}

TEST_CASE("Several graphics devices can exist at once")
{
    // Every viewport window makes its own, so nothing limits them to one.
    std::unique_ptr<GraphicsDevice> first = createDeviceOrSkip();
    std::unique_ptr<GraphicsDevice> second = createDeviceOrSkip();

    REQUIRE(first->getFunction("meshVertex"));
    REQUIRE(second->getFunction("meshVertex"));

    first.reset();
    REQUIRE(second->getFunction("gridFragment"));
}

TEST_CASE("A graphics device holds every shader function the passes draw with")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();

    for (const char* name : {"meshVertex",
                             "meshShadedFragment",
                             "meshWireframeFragment",
                             "pointsVertex",
                             "pointsFragment",
                             "gridVertex",
                             "gridFragment",
                             "cameraPrimVertex",
                             "cameraPrimFragment"})
    {
        INFO(name);
        REQUIRE(device->getFunction(name));
    }

    REQUIRE_FALSE(device->getFunction("noSuchFunction"));
}

TEST_CASE("A shader library that is not there is named in the error")
{
    if (!GraphicsDevice::isMetalAvailable()) SKIP(GraphicsDevice::noDeviceMessage);

    // A path the caller names is the only one looked at, so a typo never falls back on another library.
    try
    {
        GraphicsDevice device(nullptr, "/nonexistent/helix.metallib");
        FAIL("a device was made without its shaders");
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        REQUIRE(message.find("helix.metallib") != std::string::npos);
        REQUIRE(message.find("/nonexistent") != std::string::npos);
    }
}

TEST_CASE("A machine with no Metal device is told so in a fixed message")
{
    // The smoke test of the CI looks for this exact text.
    REQUIRE(std::string(GraphicsDevice::noDeviceMessage) == "No Metal device available");
}
