#include "Graphics/GraphicsDevice.h"
#include "Graphics/ViewportRenderer.h"
#include "GraphicsTestUtils.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace enzo::gfx;

TEST_CASE("A viewport renderer draws into an image of the frame size")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame;
    frame.pixelSize = {64, 32};
    const VulkanImage image = renderer.render(frame);

    REQUIRE(image.image != nullptr);
    REQUIRE(image.size == frame.pixelSize);
}

TEST_CASE("A viewport renderer keeps its image until the frame size changes")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame;
    frame.pixelSize = {64, 32};
    const VulkanImage first = renderer.render(frame);

    frame.backgroundColor = {1.f, 0.f, 1.f, 1.f};
    REQUIRE(renderer.render(frame).image == first.image);

    frame.pixelSize = {128, 64};
    const VulkanImage resized = renderer.render(frame);
    REQUIRE(resized.image != first.image);
    REQUIRE(resized.size == frame.pixelSize);
}

TEST_CASE("A viewport renderer draws meshes, points and cameras")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    auto geometry = std::make_shared<DisplayGeometry>();
    geometry->positions = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}};
    geometry->normals = {{0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}};
    geometry->topology.triangleIndices = {0, 1, 2};
    geometry->topology.edgeIndices = {0, 1, 1, 2, 2, 0};
    geometry->soloPointPositions = {{2.f, 0.f, 0.f}};
    geometry->cameraTransforms = {glm::mat4(1.f)};

    FrameState frame;
    frame.pixelSize = {64, 32};
    frame.geometry = geometry;
    REQUIRE(renderer.render(frame).image != nullptr);
}

TEST_CASE("A viewport renderer draws a frame with no pixels into no image")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    // A window that is hidden has no pixels, and nothing is drawn for it.
    FrameState frame;
    frame.pixelSize = {0, 0};
    const VulkanImage image = renderer.render(frame);

    REQUIRE(image.image == nullptr);
    REQUIRE(image.size == glm::uvec2{0, 0});
}

TEST_CASE("A viewport renderer draws a frame again when the frame size changes")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame;
    frame.pixelSize = {64, 32};
    const VulkanImage first = renderer.render(frame);

    // A window that moves to a screen with a different scale changes the size of the
    // frame in device pixels.
    frame.pixelSize = {128, 64};
    frame.pixelRatio = 2.f;
    const VulkanImage scaled = renderer.render(frame);

    REQUIRE(scaled.image != first.image);
    REQUIRE(scaled.size == glm::uvec2{128, 64});
}

TEST_CASE("A viewport renderer hands the pixels of a frame to a host that cannot sample it")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame;
    frame.pixelSize = {64, 32};
    frame.backgroundColor = {0.f, 0.5f, 1.f, 1.f};
    const PresentedFrame presented = renderer.renderToPixels(frame);

    REQUIRE(presented.size == frame.pixelSize);
    REQUIRE(presented.pixels.size() == 64u * 32u * 4u);

    // The background of the frame is the colour the host asked for, in the order the
    // pixels are laid out in memory.
    REQUIRE(presented.pixels[0] == 0);
    REQUIRE(presented.pixels[1] >= 127);
    REQUIRE(presented.pixels[1] <= 128);
    REQUIRE(presented.pixels[2] == 255);
    REQUIRE(presented.pixels[3] == 255);
}

TEST_CASE("A viewport renderer hands the pixels of a frame that changed size again")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame;
    frame.pixelSize = {64, 32};
    const PresentedFrame first = renderer.renderToPixels(frame);

    frame.pixelSize = {32, 16};
    const PresentedFrame resized = renderer.renderToPixels(frame);

    REQUIRE(resized.size == glm::uvec2{32, 16});
    REQUIRE(resized.pixels.size() == 32u * 16u * 4u);
    REQUIRE(resized.pixels.size() != first.pixels.size());
}

TEST_CASE("The pixels of a frame are copied out of the rows of the device")
{
    // The rows of a texture are padded to the alignment of the device, so the rows of the
    // host take every row of the texture on its own.
    const std::uint32_t width = 2;
    const std::uint32_t height = 3;
    const std::size_t rowBytes = width * 4;
    const std::size_t stride = rowBytes + 4;

    std::vector<std::uint8_t> texture(stride * height, 0xAA);
    for (std::uint32_t row = 0; row < height; ++row)
    {
        for (std::uint32_t column = 0; column < width; ++column)
        {
            texture[row * stride + column * 4 + 0] = static_cast<std::uint8_t>(row);
            texture[row * stride + column * 4 + 1] = static_cast<std::uint8_t>(column);
        }
    }

    std::vector<std::uint8_t> pixels(rowBytes * height, 0);
    copyFrameRows(pixels.data(), texture.data(), stride, width, height);

    for (std::uint32_t row = 0; row < height; ++row)
    {
        for (std::uint32_t column = 0; column < width; ++column)
        {
            const std::size_t index = row * rowBytes + column * 4;
            REQUIRE(pixels[index + 0] == static_cast<std::uint8_t>(row));
            REQUIRE(pixels[index + 1] == static_cast<std::uint8_t>(column));
        }
    }
}
