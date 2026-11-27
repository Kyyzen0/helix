#include "Graphics/GraphicsDevice.h"
#include "Graphics/Metal.h"
#include "HelixCore/HelixCore.h"
#include "TestPaths.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace core = helix::core;
namespace fs = std::filesystem;

namespace {

struct ViewportFixture
{
    ViewportFixture()
    {
        REQUIRE(core::initialize({}).ok);
        REQUIRE(core::newScene().ok);
        core::resetViewport();
    }
    ~ViewportFixture() { core::stopViewport(); }
};

void showCube()
{
    const core::CreateNodeResult cube = core::createNode("helix::cube", "", 0.f, 0.f);
    REQUIRE(cube.status.ok);
    REQUIRE(core::setDisplayNode(cube.id).ok);
}

void skipWithoutMetal()
{
    if (!helix::gfx::GraphicsDevice::isMetalAvailable())
        SKIP(helix::gfx::GraphicsDevice::noDeviceMessage);
}

// Pure red faces and no wireframe leave pixels that only the mesh can have, so the grid does not
// count as geometry.
core::ViewportFrameOptions redOptions(std::uint32_t width, std::uint32_t height)
{
    core::ViewportFrameOptions options;
    options.pixelWidth = width;
    options.pixelHeight = height;
    options.geometryRed = 1.f;
    options.geometryGreen = 0.f;
    options.geometryBlue = 0.f;
    options.wireframe = core::WireframeMode::Off;
    return options;
}

struct DecodedImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

// Reads a PNG back with ImageIO, as RGBA8.
DecodedImage decodePng(const std::string& path)
{
    DecodedImage image;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr,
        reinterpret_cast<const UInt8*>(path.c_str()),
        CFIndex(path.size()),
        false
    );
    CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (!source) return image;
    CGImageRef cgImage = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (!cgImage) return image;

    image.width = std::uint32_t(CGImageGetWidth(cgImage));
    image.height = std::uint32_t(CGImageGetHeight(cgImage));
    image.rgba.assign(std::size_t(image.width) * image.height * 4, 0);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(
        image.rgba.data(),
        image.width,
        image.height,
        8,
        std::size_t(image.width) * 4,
        colorSpace,
        kCGImageAlphaPremultipliedLast
    );
    CGContextDrawImage(context, CGRectMake(0, 0, image.width, image.height), cgImage);
    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);
    CGImageRelease(cgImage);
    return image;
}

std::size_t countRed(const DecodedImage& image)
{
    std::size_t count = 0;
    for (std::size_t index = 0; index + 3 < image.rgba.size(); index += 4)
    {
        if (image.rgba[index] > image.rgba[index + 1] + 40 &&
            image.rgba[index] > image.rgba[index + 2] + 40)
            ++count;
    }
    return count;
}

} // namespace

TEST_CASE_METHOD(
    ViewportFixture,
    "The viewport geometry reports the counts and the bounds of the display node"
)
{
    showCube();
    const core::ViewportGeometryInfo info = core::updateViewportGeometry();
    REQUIRE(info.status.ok);
    REQUIRE(info.pointCount == 8);
    REQUIRE(info.triangleCount == 12);
    REQUIRE(info.bounds.valid);
    REQUIRE(info.bounds.minX == -0.5f);
    REQUIRE(info.bounds.maxX == 0.5f);
    REQUIRE(info.bounds.minY == -0.5f);
    REQUIRE(info.bounds.maxY == 0.5f);
    REQUIRE(info.bounds.minZ == -0.5f);
    REQUIRE(info.bounds.maxZ == 0.5f);
}

TEST_CASE_METHOD(
    ViewportFixture,
    "A viewport with no display node says so and has nothing to frame"
)
{
    const core::ViewportGeometryInfo info = core::updateViewportGeometry();
    REQUIRE_FALSE(info.status.ok);
    REQUIRE_FALSE(info.status.error.empty());
    REQUIRE_FALSE(info.bounds.valid);
    REQUIRE_FALSE(core::frameViewport(1.f));

    // The geometry of a node that was displayed is dropped once nothing is.
    showCube();
    REQUIRE(core::updateViewportGeometry().status.ok);
    REQUIRE(core::frameViewport(1.f));
    REQUIRE(core::newScene().ok);
    REQUIRE_FALSE(core::updateViewportGeometry().status.ok);
    REQUIRE_FALSE(core::frameViewport(1.f));
}

TEST_CASE_METHOD(
    ViewportFixture,
    "Rendering to an image writes a PNG of the size asked for with the geometry in it"
)
{
    skipWithoutMetal();
    showCube();

    const std::string path = helix::test::tempPath("helix_core_viewport_render.png");
    fs::remove(path);
    REQUIRE(core::renderToImage(path, redOptions(160, 120), false).ok);
    REQUIRE(fs::exists(path));
    REQUIRE_FALSE(fs::exists(path + ".tmp"));

    // The PNG signature, then the width and the height of the IHDR chunk.
    std::ifstream file(path, std::ios::binary);
    std::vector<unsigned char> header(24);
    file.read(reinterpret_cast<char*>(header.data()), 24);
    REQUIRE(header[1] == 'P');
    REQUIRE(header[2] == 'N');
    REQUIRE(header[3] == 'G');
    REQUIRE(((header[16] << 24) | (header[17] << 16) | (header[18] << 8) | header[19]) == 160);
    REQUIRE(((header[20] << 24) | (header[21] << 16) | (header[22] << 8) | header[23]) == 120);

    // The cube is framed, so it takes a good part of the picture.
    const DecodedImage image = decodePng(path);
    REQUIRE(image.width == 160);
    REQUIRE(image.height == 120);
    REQUIRE(countRed(image) > 160 * 120 / 20);
}

TEST_CASE_METHOD(
    ViewportFixture,
    "Rendering to an image refuses what cannot be drawn and leaves no file"
)
{
    skipWithoutMetal();
    const std::string path = helix::test::tempPath("helix_core_viewport_refused.png");
    fs::remove(path);

    // Nothing is displayed.
    const core::Status noDisplay = core::renderToImage(path, redOptions(64, 64), false);
    REQUIRE_FALSE(noDisplay.ok);
    REQUIRE_FALSE(noDisplay.error.empty());
    REQUIRE_FALSE(fs::exists(path));

    showCube();
    REQUIRE_FALSE(core::renderToImage(path, redOptions(0, 64), false).ok);
    REQUIRE_FALSE(core::renderToImage("", redOptions(64, 64), false).ok);
    REQUIRE_FALSE(
        core::renderToImage("/nonexistent_folder_for_helix/out.png", redOptions(64, 64), false).ok
    );
    REQUIRE_FALSE(fs::exists(path));
}

TEST_CASE_METHOD(
    ViewportFixture,
    "Framing makes the geometry larger in the view, and reset puts the camera back"
)
{
    skipWithoutMetal();
    showCube();
    REQUIRE(core::updateViewportGeometry().status.ok);

    const std::string path = helix::test::tempPath("helix_core_viewport_camera.png");
    const auto render = [&] {
        REQUIRE(core::renderToImage(path, redOptions(200, 150), true).ok);
        return decodePng(path);
    };

    const DecodedImage initial = render();
    REQUIRE(initial.width == 200);

    // A cube one unit wide is small seen from the distance of a new camera.
    REQUIRE(core::frameViewport(200.f / 150.f));
    const DecodedImage framed = render();
    REQUIRE(countRed(framed) > 4 * countRed(initial));
    REQUIRE(framed.rgba != initial.rgba);

    // Orbit, pan and dolly each change the picture.
    core::orbitViewport(60.f, 20.f);
    const DecodedImage orbited = render();
    REQUIRE(orbited.rgba != framed.rgba);
    core::panViewport(40.f, 10.f);
    const DecodedImage panned = render();
    REQUIRE(panned.rgba != orbited.rgba);
    core::dollyViewport(2.f);
    const DecodedImage dollied = render();
    REQUIRE(dollied.rgba != panned.rgba);

    core::resetViewport();
    REQUIRE(render().rgba == initial.rgba);
}

TEST_CASE_METHOD(
    ViewportFixture,
    "The viewport records a frame into the command buffer of its caller"
)
{
    skipWithoutMetal();
    showCube();
    REQUIRE(core::updateViewportGeometry().status.ok);
    REQUIRE(core::frameViewport(1.f));

    // Nothing to encode into before the viewport starts.
    REQUIRE_FALSE(core::isViewportRunning());
    REQUIRE_FALSE(core::encodeViewport(nullptr, redOptions(64, 64)).status.ok);

    REQUIRE(core::startViewport(nullptr).ok);
    REQUIRE(core::isViewportRunning());
    REQUIRE_FALSE(core::encodeViewport(nullptr, redOptions(64, 64)).status.ok);

    MTL::Device* device = MTL::CreateSystemDefaultDevice();
    REQUIRE(device != nullptr);
    MTL::CommandQueue* queue = device->newCommandQueue();
    MTL::CommandBuffer* commandBuffer = queue->commandBuffer();

    const core::ViewportFrame frame = core::encodeViewport(commandBuffer, redOptions(64, 48));
    REQUIRE(frame.status.ok);
    REQUIRE(frame.colorTexture != nullptr);
    REQUIRE(frame.pixelWidth == 64);
    REQUIRE(frame.pixelHeight == 48);

    // A frame with no pixels draws nothing and is not an error.
    const core::ViewportFrame empty = core::encodeViewport(commandBuffer, redOptions(0, 0));
    REQUIRE(empty.status.ok);
    REQUIRE(empty.colorTexture == nullptr);

    commandBuffer->commit();
    commandBuffer->waitUntilCompleted();

    auto* texture = static_cast<MTL::Texture*>(frame.colorTexture);
    std::vector<std::uint8_t> pixels(64 * 48 * 4);
    texture->getBytes(pixels.data(), 64 * 4, MTL::Region::Make2D(0, 0, 64, 48), 0);
    std::size_t red = 0;
    for (std::size_t index = 0; index < pixels.size(); index += 4)
        if (pixels[index] > pixels[index + 1] + 40) ++red;
    REQUIRE(red > 64 * 48 / 10);

    queue->release();
    device->release();
    core::stopViewport();
    REQUIRE_FALSE(core::isViewportRunning());
    REQUIRE_FALSE(core::encodeViewport(nullptr, redOptions(64, 64)).status.ok);
}
