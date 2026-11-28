#include "Graphics/GraphicsDevice.h"
#include "Graphics/Metal.h"
#include "HelixCore/HelixCore.h"
#include "TestPaths.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
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

TEST_CASE_METHOD(ViewportFixture, "The viewport reports its camera angles, distance and grid cell")
{
    core::resetViewport();
    core::ViewportCameraInfo info = core::viewportCameraInfo();
    // The home view is the front-right three-quarter view, which shows the front, the right and the
    // top of a model. It was -135, which looks at it from behind.
    REQUIRE(info.azimuthDegrees == Catch::Approx(45.f));
    REQUIRE(info.elevationDegrees == Catch::Approx(25.f));
    REQUIRE(info.distance == Catch::Approx(15.f));
    REQUIRE(info.gridCellSize == Catch::Approx(2.f));

    core::orbitViewportTo(90.f, 0.f);
    info = core::viewportCameraInfo();
    REQUIRE(info.azimuthDegrees == Catch::Approx(90.f));
    REQUIRE(info.elevationDegrees == Catch::Approx(0.f).margin(1e-4));

    // The top is held at 89 degrees, which the report says as it is.
    core::orbitViewportTo(0.f, 90.f);
    REQUIRE(core::viewportCameraInfo().elevationDegrees == Catch::Approx(89.f));

    core::dollyViewport(60.f);
    info = core::viewportCameraInfo();
    REQUIRE(info.distance > 15.f);
    REQUIRE(info.gridCellSize > 2.f);
    core::resetViewport();
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

TEST_CASE_METHOD(
    ViewportFixture,
    "An absolute orbit gives the same picture whatever the camera did before"
)
{
    skipWithoutMetal();
    showCube();
    REQUIRE(core::updateViewportGeometry().status.ok);

    const auto render = [](const char* name, float azimuth, float elevation, bool jostle) {
        core::resetViewport();
        if (jostle) core::orbitViewport(233.f, -61.f);
        core::orbitViewportTo(azimuth, elevation);
        REQUIRE(core::frameViewport(4.f / 3.f));
        const std::string path = helix::test::tempPath(name);
        fs::remove(path);
        REQUIRE(core::renderToImage(path, redOptions(96, 72), true).ok);
        return decodePng(path);
    };

    const DecodedImage first = render("helix_orbit_a.png", 30.f, 35.f, false);
    const DecodedImage again = render("helix_orbit_b.png", 30.f, 35.f, true);
    const DecodedImage other = render("helix_orbit_c.png", 120.f, 10.f, false);
    REQUIRE(first.rgba.size() == 96 * 72 * 4);
    REQUIRE(first.rgba == again.rgba);
    REQUIRE(first.rgba != other.rgba);
    REQUIRE(countRed(other) > 96 * 72 / 20);

    // An angle that is not a number leaves the camera where it was.
    core::orbitViewportTo(30.f, 35.f);
    core::orbitViewportTo(std::nanf(""), 0.f);
    REQUIRE(core::frameViewport(4.f / 3.f));
}

// ---------------------------------------------------------------------------------------------
// Materials and textures
// ---------------------------------------------------------------------------------------------

namespace {

struct Colour
{
    int r, g, b;
};

// The mean colour of the square of pixels around (x, y), with x and y as fractions of the picture.
Colour sampleAt(const DecodedImage& image, float fractionX, float fractionY)
{
    const int centerX = int(fractionX * float(image.width));
    const int centerY = int(fractionY * float(image.height));
    long red = 0, green = 0, blue = 0, count = 0;
    for (int y = centerY - 3; y <= centerY + 3; ++y)
    {
        for (int x = centerX - 3; x <= centerX + 3; ++x)
        {
            const std::size_t index = (std::size_t(y) * image.width + std::size_t(x)) * 4;
            red += image.rgba[index];
            green += image.rgba[index + 1];
            blue += image.rgba[index + 2];
            ++count;
        }
    }
    return {int(red / count), int(green / count), int(blue / count)};
}

bool isRed(const Colour& c) { return c.r > c.g + 60 && c.r > c.b + 60; }
bool isGreen(const Colour& c) { return c.g > c.r + 60 && c.g > c.b + 60; }
bool isBlue(const Colour& c) { return c.b > c.r + 60 && c.b > c.g + 60; }
// Bright and without a dominant channel; the factor of the material tints it a little.
bool isWhitish(const Colour& c)
{
    const int high = std::max({c.r, c.g, c.b});
    const int low = std::min({c.r, c.g, c.b});
    return low > 90 && low * 10 > high * 6;
}

// Displays the file, faces it head on, and renders it at 200 x 200 without wireframe.
DecodedImage renderImported(const fs::path& file, bool materialsVisible, const char* name)
{
    const core::CreateNodeResult imported = core::importFile(file.string());
    REQUIRE(imported.status.ok);
    REQUIRE(core::setDisplayNode(imported.id).ok);
    REQUIRE(core::updateViewportGeometry().status.ok);
    core::resetViewport();
    // The quad lies on the z = 0 plane and faces +z, so the camera at azimuth 0 looks straight at it.
    core::orbitViewportTo(0.f, 0.f);
    REQUIRE(core::frameViewport(1.f));

    core::ViewportFrameOptions options;
    options.pixelWidth = 200;
    options.pixelHeight = 200;
    options.wireframe = core::WireframeMode::Off;
    options.materialsVisible = materialsVisible;
    const std::string path = helix::test::tempPath(name);
    fs::remove(path);
    REQUIRE(core::renderToImage(path, options, true).ok);
    return decodePng(path);
}

fs::path importData(const char* name) { return fs::path(HELIX_TEST_DATA_DIR) / "import" / name; }

// The four quadrants of the picture, drawn upright: red at the top left, green at the top right,
// blue at the bottom left and white at the bottom right.
void requireQuadrantsUpright(const DecodedImage& image)
{
    REQUIRE(image.width == 200);
    const Colour topLeft = sampleAt(image, 0.38f, 0.38f);
    const Colour topRight = sampleAt(image, 0.62f, 0.38f);
    const Colour bottomLeft = sampleAt(image, 0.38f, 0.62f);
    const Colour bottomRight = sampleAt(image, 0.62f, 0.62f);
    INFO("top left " << topLeft.r << "," << topLeft.g << "," << topLeft.b);
    INFO("top right " << topRight.r << "," << topRight.g << "," << topRight.b);
    INFO("bottom left " << bottomLeft.r << "," << bottomLeft.g << "," << bottomLeft.b);
    INFO("bottom right " << bottomRight.r << "," << bottomRight.g << "," << bottomRight.b);
    REQUIRE(isRed(topLeft));
    REQUIRE(isGreen(topRight));
    REQUIRE(isBlue(bottomLeft));
    REQUIRE(isWhitish(bottomRight));
}

// Hidden materials: the faces take the geometry colour, so the quad is one flat grey.
void requireGrey(const DecodedImage& image)
{
    const Colour a = sampleAt(image, 0.38f, 0.38f);
    const Colour b = sampleAt(image, 0.62f, 0.38f);
    const Colour c = sampleAt(image, 0.38f, 0.62f);
    const Colour d = sampleAt(image, 0.62f, 0.62f);
    for (const Colour& colour : {a, b, c, d})
    {
        REQUIRE(std::abs(colour.r - a.r) < 24);
        REQUIRE(std::abs(colour.g - a.g) < 24);
        REQUIRE(std::abs(colour.b - a.b) < 24);
    }
    REQUIRE(std::abs(a.r - a.b) < 40);
    REQUIRE(a.r > 60);
}

} // namespace

TEST_CASE_METHOD(ViewportFixture, "The viewport draws the picture of a glb material upright", "[viewport][material]")
{
    skipWithoutMetal();
    requireQuadrantsUpright(renderImported(importData("textured-quad.glb"), true, "helix_material_glb.png"));
}

TEST_CASE_METHOD(ViewportFixture, "The viewport draws the picture of a gltf with an external file", "[viewport][material]")
{
    skipWithoutMetal();
    requireQuadrantsUpright(renderImported(importData("textured-quad.gltf"), true, "helix_material_gltf.png"));
}

TEST_CASE_METHOD(ViewportFixture, "The viewport draws the picture of a usd material", "[viewport][material]")
{
    skipWithoutMetal();
    requireQuadrantsUpright(renderImported(importData("textured-quad.usda"), true, "helix_material_usda.png"));
}

TEST_CASE_METHOD(ViewportFixture, "Hidden materials draw every face in the geometry colour", "[viewport][material]")
{
    skipWithoutMetal();
    const DecodedImage hidden = renderImported(importData("textured-quad.glb"), false, "helix_material_hidden.png");
    requireGrey(hidden);

    REQUIRE(core::newScene().ok);
    const DecodedImage shown = renderImported(importData("textured-quad.glb"), true, "helix_material_shown.png");
    REQUIRE(shown.rgba != hidden.rgba);
}

TEST_CASE_METHOD(ViewportFixture, "A material with no picture draws in its base colour factor", "[viewport][material]")
{
    skipWithoutMetal();
    // Two quads side by side, the first red and the second blue.
    const core::CreateNodeResult imported = core::importFile(importData("two-materials.glb").string());
    REQUIRE(imported.status.ok);
    REQUIRE(core::setDisplayNode(imported.id).ok);
    REQUIRE(core::updateViewportGeometry().status.ok);
    core::resetViewport();
    core::orbitViewportTo(0.f, 0.f);
    REQUIRE(core::frameViewport(2.f));

    core::ViewportFrameOptions options;
    options.pixelWidth = 400;
    options.pixelHeight = 200;
    options.wireframe = core::WireframeMode::Off;
    const std::string path = helix::test::tempPath("helix_material_factor.png");
    fs::remove(path);
    REQUIRE(core::renderToImage(path, options, true).ok);
    const DecodedImage image = decodePng(path);
    REQUIRE(isRed(sampleAt(image, 0.30f, 0.5f)));
    REQUIRE(isBlue(sampleAt(image, 0.70f, 0.5f)));
}

TEST_CASE_METHOD(ViewportFixture, "The adaptive wireframe leaves a textured mesh alone", "[viewport][material]")
{
    skipWithoutMetal();
    const core::CreateNodeResult imported = core::importFile(importData("textured-quad.glb").string());
    REQUIRE(imported.status.ok);
    REQUIRE(core::setDisplayNode(imported.id).ok);
    REQUIRE(core::updateViewportGeometry().status.ok);
    core::resetViewport();
    core::orbitViewportTo(0.f, 0.f);
    REQUIRE(core::frameViewport(1.f));

    const auto render = [](core::WireframeMode mode, bool materials, const char* name) {
        core::ViewportFrameOptions options;
        options.pixelWidth = 200;
        options.pixelHeight = 200;
        options.wireframe = mode;
        options.materialsVisible = materials;
        const std::string path = helix::test::tempPath(name);
        fs::remove(path);
        REQUIRE(core::renderToImage(path, options, true).ok);
        return decodePng(path);
    };
    const DecodedImage off = render(core::WireframeMode::Off, true, "helix_material_wire_off.png");
    const DecodedImage automatic = render(core::WireframeMode::Auto, true, "helix_material_wire_auto.png");
    const DecodedImage on = render(core::WireframeMode::On, true, "helix_material_wire_on.png");
    REQUIRE(automatic.rgba == off.rgba);
    REQUIRE(on.rgba != off.rgba);

    // With the materials hidden the mesh is plain again, and the adaptive wireframe comes back.
    const DecodedImage plainOff = render(core::WireframeMode::Off, false, "helix_material_wire_plain_off.png");
    const DecodedImage plainAuto = render(core::WireframeMode::Auto, false, "helix_material_wire_plain_auto.png");
    REQUIRE(plainAuto.rgba != plainOff.rgba);
}
