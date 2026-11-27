#include "Graphics/GraphicsDevice.h"
#include "Graphics/Passes/MeshPass.h"
#include "Graphics/ViewportRenderer.h"
#include "GraphicsTestUtils.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/geometric.hpp>
#include <memory>
#include <set>

using namespace helix::gfx;

namespace {

constexpr float pi = 3.14159265f;

// Returns a flat n by n grid of quads on the ground plane, as 2 n^2 triangles, with its edges.
std::shared_ptr<DisplayGeometry> makeGrid(int n, float size)
{
    auto geometry = std::make_shared<DisplayGeometry>();
    for (int j = 0; j <= n; ++j)
    {
        for (int i = 0; i <= n; ++i)
        {
            geometry->positions.push_back({(float(i) / float(n) - 0.5f) * size, 0.f, (float(j) / float(n) - 0.5f) * size});
            geometry->normals.push_back({0.f, 1.f, 0.f});
        }
    }
    auto at = [n](int i, int j) { return std::uint32_t(j * (n + 1) + i); };
    for (int j = 0; j < n; ++j)
    {
        for (int i = 0; i < n; ++i)
        {
            geometry->topology.triangleIndices.insert(
                geometry->topology.triangleIndices.end(),
                {at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j), at(i + 1, j + 1), at(i, j + 1)}
            );
            geometry->topology.edgeIndices.insert(
                geometry->topology.edgeIndices.end(),
                {at(i, j), at(i + 1, j), at(i, j), at(i, j + 1), at(i + 1, j + 1), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)}
            );
        }
    }
    return geometry;
}

// Returns a sphere of radius 2 centred on the origin, with smooth normals.
std::shared_ptr<DisplayGeometry> makeSphere()
{
    auto geometry = std::make_shared<DisplayGeometry>();
    constexpr int rings = 24;
    constexpr int segments = 48;
    for (int r = 0; r <= rings; ++r)
    {
        const float theta = pi * float(r) / float(rings);
        for (int s = 0; s <= segments; ++s)
        {
            const float phi = 2.f * pi * float(s) / float(segments);
            const glm::vec3 normal(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            geometry->positions.push_back(normal * 2.f);
            geometry->normals.push_back(normal);
        }
    }
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < segments; ++s)
        {
            const std::uint32_t a = std::uint32_t(r * (segments + 1) + s);
            const std::uint32_t b = a + std::uint32_t(segments + 1);
            geometry->topology.triangleIndices.insert(geometry->topology.triangleIndices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
    return geometry;
}

float luminance(const Rgba& p) { return 0.2126f * float(p.r) + 0.7152f * float(p.g) + 0.0722f * float(p.b); }

FrameState sphereFrame()
{
    FrameState frame;
    frame.pixelSize = {128, 128};
    frame.backgroundColor = {0.13f, 0.14f, 0.16f, 1.f};
    frame.geometryColor = {0.62f, 0.64f, 0.68f, 1.f};
    frame.wireframeVisible = false;
    frame.geometry = makeSphere();
    frame.camera.frameBounds({-2.f, -2.f, -2.f}, {2.f, 2.f, 2.f}, 1.f);
    return frame;
}

// The pixels of the frame that are not the background and not the grid: the sphere.
std::vector<Rgba> spherePixels(const PresentedFrame& frame)
{
    std::vector<Rgba> pixels;
    for (std::uint32_t y = 40; y < 88; ++y)
        for (std::uint32_t x = 40; x < 88; ++x) pixels.push_back(pixelAt(frame, x, y));
    return pixels;
}

} // namespace

TEST_CASE("The adaptive wireframe fades out as the edges come close", "[viewport]")
{
    REQUIRE(adaptiveWireframeOpacity(0.f) == 0.f);
    REQUIRE(adaptiveWireframeOpacity(kWireframeGoneEdgePoints) == 0.f);
    REQUIRE(adaptiveWireframeOpacity(kWireframeFullEdgePoints) == 1.f);
    REQUIRE(adaptiveWireframeOpacity(500.f) == 1.f);

    float previous = 0.f;
    for (float points = 0.f; points <= 8.f; points += 0.25f)
    {
        const float opacity = adaptiveWireframeOpacity(points);
        REQUIRE(opacity >= previous);
        previous = opacity;
    }
}

TEST_CASE("An adaptive wireframe is drawn on a light mesh as an ordinary one is", "[viewport]")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame;
    frame.pixelSize = {128, 128};
    frame.geometry = makeGrid(8, 6.f);
    frame.camera.frameBounds({-3.f, 0.f, -3.f}, {3.f, 0.f, 3.f}, 1.f);
    frame.wireframeVisible = true;

    frame.wireframeAdaptive = false;
    const PresentedFrame plain = renderer.renderToPixels(frame);
    frame.wireframeAdaptive = true;
    const PresentedFrame adaptive = renderer.renderToPixels(frame);
    frame.wireframeVisible = false;
    const PresentedFrame none = renderer.renderToPixels(frame);

    REQUIRE_FALSE(samePixels(plain, none));
    // 128 triangles, edges of about 0.75 units that are tens of points long: fully opaque.
    REQUIRE(samePixels(adaptive, plain));
}

TEST_CASE("An adaptive wireframe is left out of a mesh with more than 50 000 triangles", "[viewport]")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    // 2 * 160^2 = 51 200 triangles.
    FrameState frame;
    frame.pixelSize = {128, 128};
    frame.geometry = makeGrid(160, 6.f);
    REQUIRE(frame.geometry->topology.triangleIndices.size() / 3 > kWireframeAutoTriangleLimit);
    frame.camera.frameBounds({-3.f, 0.f, -3.f}, {3.f, 0.f, 3.f}, 1.f);

    frame.wireframeVisible = true;
    frame.wireframeAdaptive = true;
    const PresentedFrame adaptive = renderer.renderToPixels(frame);
    frame.wireframeVisible = false;
    const PresentedFrame none = renderer.renderToPixels(frame);
    frame.wireframeVisible = true;
    frame.wireframeAdaptive = false;
    const PresentedFrame forced = renderer.renderToPixels(frame);

    // Auto leaves the lines out, and On draws them whatever the density.
    REQUIRE(samePixels(adaptive, none));
    REQUIRE_FALSE(samePixels(forced, none));
}

TEST_CASE("An adaptive wireframe fades out when the edges are a pixel apart", "[viewport]")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    // 40 000 triangles on a 64 pixel frame: the edges are far under a pixel long.
    FrameState frame;
    frame.pixelSize = {64, 64};
    frame.geometry = makeGrid(140, 6.f);
    REQUIRE(frame.geometry->topology.triangleIndices.size() / 3 <= kWireframeAutoTriangleLimit);
    frame.camera.frameBounds({-3.f, 0.f, -3.f}, {3.f, 0.f, 3.f}, 1.f);

    frame.wireframeVisible = true;
    frame.wireframeAdaptive = true;
    const PresentedFrame adaptive = renderer.renderToPixels(frame);
    frame.wireframeVisible = false;
    const PresentedFrame none = renderer.renderToPixels(frame);
    REQUIRE(samePixels(adaptive, none));
}

TEST_CASE("A mesh is lit so that its volume reads, whatever the angle of the camera", "[viewport]")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame = sphereFrame();
    for (const glm::vec2 orbit : {glm::vec2(0.f, 0.f), glm::vec2(150.f, 0.f), glm::vec2(0.f, 90.f), glm::vec2(300.f, -60.f)})
    {
        FrameState turned = frame;
        turned.camera.orbit(orbit);
        const PresentedFrame presented = renderer.renderToPixels(turned);

        // The sphere shows many tones, from a lit side to a shaded one, and none of them is
        // crushed to black or blown out to white.
        std::set<int> tones;
        float darkest = 255.f;
        float brightest = 0.f;
        for (const Rgba& pixel : spherePixels(presented))
        {
            tones.insert(int(luminance(pixel)) / 4);
            darkest = std::min(darkest, luminance(pixel));
            brightest = std::max(brightest, luminance(pixel));
        }
        REQUIRE(tones.size() >= 12);
        REQUIRE(darkest > 20.f);
        REQUIRE(brightest < 250.f);
        REQUIRE(brightest - darkest > 40.f);
    }
}

TEST_CASE("The light follows the camera: the upper left of a sphere is brighter than the lower right", "[viewport]")
{
    std::unique_ptr<GraphicsDevice> device = createDeviceOrSkip();
    ViewportRenderer renderer(*device);

    FrameState frame = sphereFrame();
    for (const glm::vec2 orbit : {glm::vec2(0.f, 0.f), glm::vec2(200.f, 0.f), glm::vec2(0.f, -80.f)})
    {
        FrameState turned = frame;
        turned.camera.orbit(orbit);
        const PresentedFrame presented = renderer.renderToPixels(turned);
        const float upperLeft = luminance(pixelAt(presented, 52, 52));
        const float lowerRight = luminance(pixelAt(presented, 76, 76));
        REQUIRE(upperLeft > lowerRight);
    }
}
