#include "Graphics/GraphicsDevice.h"
#include "Graphics/ViewportRenderer.h"
#include "GraphicsTestUtils.h"
#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace helix::gfx;

namespace {

constexpr Rgba black{0, 0, 0, 255};

/// @brief Returns a frame of the size with a black background and no geometry.
FrameState makeFrame(std::uint32_t width, std::uint32_t height)
{
    FrameState frame;
    frame.pixelSize = {width, height};
    frame.backgroundColor = {0.f, 0.f, 0.f, 1.f};
    return frame;
}

/// @brief Returns a triangle on the ground plane that covers the origin, facing up.
std::shared_ptr<DisplayGeometry> makeTriangle()
{
    auto geometry = std::make_shared<DisplayGeometry>();
    geometry->positions = {{-5.f, 0.f, -5.f}, {5.f, 0.f, -5.f}, {0.f, 0.f, 6.f}};
    geometry->normals = {{0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}};
    geometry->topology.triangleIndices = {0, 1, 2};
    geometry->topology.edgeIndices = {0, 1, 1, 2, 2, 0};
    return geometry;
}

std::size_t countPixels(const PresentedFrame& frame, auto predicate)
{
    std::size_t count = 0;
    for (std::uint32_t y = 0; y < frame.size.y; ++y)
    {
        for (std::uint32_t x = 0; x < frame.size.x; ++x)
        {
            if (predicate(pixelAt(frame, x, y))) ++count;
        }
    }
    return count;
}

/// @brief Returns whether the pixel is the orange of a camera frame.
bool isOrange(const Rgba& pixel)
{
    return pixel.r > 150 && pixel.r > pixel.g + 30 && pixel.g > pixel.b + 30;
}

} // namespace

TEST_CASE("A viewport renderer draws into a texture of the frame size")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame = makeFrame(64, 32);
    const RenderedImage image = renderer.render(frame);

    REQUIRE(image.texture != nullptr);
    REQUIRE(image.size == frame.pixelSize);
    REQUIRE(image.texture->width() == 64);
    REQUIRE(image.texture->height() == 32);
    REQUIRE(image.texture->pixelFormat() == MTL::PixelFormatRGBA8Unorm);
}

TEST_CASE("A viewport renderer keeps its texture until the frame size changes")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame = makeFrame(64, 32);
    const RenderedImage first = renderer.render(frame);

    frame.backgroundColor = {1.f, 0.f, 1.f, 1.f};
    REQUIRE(renderer.render(frame).texture == first.texture);

    frame.pixelSize = {128, 64};
    const RenderedImage resized = renderer.render(frame);
    REQUIRE(resized.texture != first.texture);
    REQUIRE(resized.size == frame.pixelSize);

    // The texture that was replaced stays valid for the frames a host still has in flight.
    REQUIRE(first.texture->width() == 64);
}

TEST_CASE("A viewport renderer draws a frame with no pixels into no texture")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    // A window that is hidden has no pixels, and nothing is drawn for it.
    const FrameState frame = makeFrame(0, 0);
    const RenderedImage image = renderer.render(frame);

    REQUIRE(image.texture == nullptr);
    REQUIRE(image.size == glm::uvec2{0, 0});

    const PresentedFrame presented = renderer.renderToPixels(frame);
    REQUIRE(presented.pixels.empty());
}

TEST_CASE("A viewport renderer draws a frame again when the frame size changes")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame = makeFrame(64, 32);
    const RenderedImage first = renderer.render(frame);

    // A window that moves to a screen with a different scale changes the size of the
    // frame in device pixels.
    frame.pixelSize = {128, 64};
    frame.pixelRatio = 2.f;
    const RenderedImage scaled = renderer.render(frame);

    REQUIRE(scaled.texture != first.texture);
    REQUIRE(scaled.size == glm::uvec2{128, 64});
}

TEST_CASE("A viewport renderer hands the pixels of a frame to a host that wants them")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame = makeFrame(64, 32);
    frame.backgroundColor = {0.f, 0.5f, 1.f, 1.f};
    const PresentedFrame presented = renderer.renderToPixels(frame);

    REQUIRE(presented.size == frame.pixelSize);
    REQUIRE(presented.pixels.size() == 64u * 32u * 4u);

    // The background of the frame is the colour the host asked for, in the order the
    // pixels are laid out in memory. The grid does not reach the corner.
    const Rgba corner = pixelAt(presented, 0, 0);
    REQUIRE(corner.r == 0);
    REQUIRE(corner.g >= 127);
    REQUIRE(corner.g <= 128);
    REQUIRE(corner.b == 255);
    REQUIRE(corner.a == 255);
}

TEST_CASE("A viewport renderer hands the pixels of a frame that changed size again")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame = makeFrame(64, 32);
    const PresentedFrame first = renderer.renderToPixels(frame);

    frame.pixelSize = {32, 16};
    const PresentedFrame resized = renderer.renderToPixels(frame);

    REQUIRE(resized.size == glm::uvec2{32, 16});
    REQUIRE(resized.pixels.size() == 32u * 16u * 4u);
    REQUIRE(resized.pixels.size() != first.pixels.size());
}

TEST_CASE("A viewport renderer draws the grid over the background")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    const PresentedFrame presented = renderer.renderToPixels(makeFrame(128, 128));

    // The lines of the grid blend a grey over the black background, and leave the target opaque.
    REQUIRE(countPixels(presented, [](const Rgba& p) { return differsFrom(p, black); }) > 50);
    REQUIRE(countPixels(presented, [](const Rgba& p) { return p.a != 255; }) == 0);
}

TEST_CASE("A viewport renderer shades a mesh in the colour the host asks for")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame = makeFrame(64, 64);
    frame.geometry = makeTriangle();
    frame.geometryColor = {1.f, 0.f, 0.f, 1.f};
    frame.wireframeVisible = false;
    const PresentedFrame presented = renderer.renderToPixels(frame);

    // The origin lies inside the triangle and projects to the middle of the frame. The light only
    // shapes the colour, so a red mesh has no green and no blue.
    const Rgba centre = pixelAt(presented, 32, 32);
    REQUIRE(centre.r > 40);
    REQUIRE(centre.g == 0);
    REQUIRE(centre.b == 0);
}

TEST_CASE("A viewport renderer draws the wireframe darker than the faces, and hides it on request")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame = makeFrame(128, 128);
    frame.geometry = makeTriangle();
    frame.geometryColor = {1.f, 0.f, 0.f, 1.f};

    frame.wireframeVisible = true;
    const PresentedFrame withWireframe = renderer.renderToPixels(frame);
    frame.wireframeVisible = false;
    const PresentedFrame withoutWireframe = renderer.renderToPixels(frame);

    REQUIRE_FALSE(samePixels(withWireframe, withoutWireframe));

    // The wireframe is a dark shade of the mesh, which no lit face is.
    const auto darkRed = [](const Rgba& p) { return p.r > 0 && p.r < 95 && p.g == 0 && p.b == 0; };
    REQUIRE(countPixels(withWireframe, darkRed) > countPixels(withoutWireframe, darkRed));
}

TEST_CASE("A viewport renderer draws the points that belong to no face")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    auto geometry = std::make_shared<DisplayGeometry>();
    geometry->soloPointPositions = {{0.f, 0.f, 0.f}};
    geometry->pointPositions = {{0.f, 0.f, 0.f}, {2.f, 0.f, 0.f}};

    FrameState frame = makeFrame(64, 64);
    frame.geometry = geometry;
    const PresentedFrame solo = renderer.renderToPixels(frame);

    // The point sits at the origin, in the middle of the frame, as a white disc.
    REQUIRE(pixelAt(solo, 32, 32) == Rgba{255, 255, 255, 255});

    // Showing every point adds the second one.
    frame.pointsVisible = true;
    const PresentedFrame all = renderer.renderToPixels(frame);
    const auto white = [](const Rgba& p) { return p.r > 200 && p.g > 200 && p.b > 200; };
    REQUIRE(countPixels(all, white) > countPixels(solo, white));
}

TEST_CASE("A viewport renderer draws a camera as a frame, and leaves out the one the view looks through")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    auto geometry = std::make_shared<DisplayGeometry>();
    geometry->cameraTransforms = {glm::mat4(1.f)};
    geometry->cameraPaths = {"/camera"};

    FrameState frame = makeFrame(256, 256);
    frame.geometry = geometry;
    REQUIRE(countPixels(renderer.renderToPixels(frame), isOrange) > 0);

    // The view looks through the camera, so its own frame is not drawn.
    frame.viewCameraIndex = 0;
    REQUIRE(countPixels(renderer.renderToPixels(frame), isOrange) == 0);

    // A view camera the geometry does not hold looks through the orbit camera instead.
    frame.viewCameraIndex = 7;
    REQUIRE(countPixels(renderer.renderToPixels(frame), isOrange) > 0);
}

TEST_CASE("A viewport renderer draws a full frame mesh that covers the background")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    // A ground plane far larger than the view.
    auto geometry = std::make_shared<DisplayGeometry>();
    geometry->positions = {{-100.f, 0.f, -100.f}, {100.f, 0.f, -100.f}, {100.f, 0.f, 100.f}, {-100.f, 0.f, 100.f}};
    geometry->normals = {{0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 1.f, 0.f}};
    geometry->topology.triangleIndices = {0, 1, 2, 0, 2, 3};

    FrameState frame = makeFrame(64, 64);
    frame.geometry = geometry;
    frame.wireframeVisible = false;
    const PresentedFrame presented = renderer.renderToPixels(frame);

    // The camera looks down at the plane, so the plane fills the frame below the horizon.
    REQUIRE(differsFrom(pixelAt(presented, 32, 63), black));
    REQUIRE(differsFrom(pixelAt(presented, 0, 63), black));
    REQUIRE(differsFrom(pixelAt(presented, 63, 63), black));
}

TEST_CASE("Two frames drawn from the same data are identical")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    auto geometry = makeTriangle();
    geometry->soloPointPositions = {{1.f, 0.f, 1.f}};
    geometry->cameraTransforms = {glm::mat4(1.f)};

    FrameState frame = makeFrame(96, 64);
    frame.geometry = geometry;
    frame.geometryColor = {0.3f, 0.7f, 0.4f, 1.f};

    const PresentedFrame first = renderer.renderToPixels(frame);
    const PresentedFrame second = renderer.renderToPixels(frame);
    REQUIRE(samePixels(first, second));

    // A renderer made afresh draws the same pixels, so nothing leaks from one frame into the next.
    ViewportRenderer other(*device);
    REQUIRE(samePixels(other.renderToPixels(frame), first));
}

TEST_CASE("A viewport renderer draws new geometry when the frame brings it")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame = makeFrame(64, 64);
    frame.geometry = makeTriangle();
    frame.wireframeVisible = false;
    const PresentedFrame triangle = renderer.renderToPixels(frame);

    frame.geometry = nullptr;
    const PresentedFrame empty = renderer.renderToPixels(frame);
    REQUIRE_FALSE(samePixels(triangle, empty));

    // Moving the camera changes the pixels without bringing new geometry.
    frame.geometry = makeTriangle();
    frame.camera.orbit({40.f, 0.f});
    REQUIRE_FALSE(samePixels(renderer.renderToPixels(frame), triangle));
}

TEST_CASE("A viewport renderer records into the command buffer of its host")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame = makeFrame(64, 48);
    frame.geometry = makeTriangle();
    const PresentedFrame reference = renderer.renderToPixels(frame);

    // The scene graph of Qt Quick owns the command buffer and commits it after its own passes.
    ViewportRenderer hosted(*device);
    std::vector<std::uint8_t> pixels(64 * 48 * 4);
    {
        ScopedAutoreleasePool pool;
        MTL::CommandBuffer* commandBuffer = device->getCommandQueue()->commandBuffer();
        const RenderedImage image = hosted.encode(frame, commandBuffer);
        REQUIRE(image.texture != nullptr);
        commandBuffer->commit();
        commandBuffer->waitUntilCompleted();
        image.texture->getBytes(pixels.data(), 64 * 4, MTL::Region::Make2D(0, 0, 64, 48), 0);
    }
    // Compares without printing the megabytes a failed vector comparison would.
    const bool same = pixels == reference.pixels;
    REQUIRE(same);
}

TEST_CASE("A viewport renderer hands out a texture before the first frame is drawn")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    // A host shows the texture in its scene before the frame that fills it is recorded.
    const RenderedImage target = renderer.prepareTarget({80, 40});
    REQUIRE(target.texture != nullptr);
    REQUIRE(target.size == glm::uvec2{80, 40});

    FrameState frame = makeFrame(80, 40);
    REQUIRE(renderer.render(frame).texture == target.texture);
    REQUIRE(renderer.prepareTarget({0, 0}).texture == nullptr);
}
