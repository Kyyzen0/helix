#include "Graphics/ViewportCamera.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

using namespace helix::gfx;
using Catch::Approx;

namespace {

glm::vec3 toViewSpace(const ViewportCamera& camera, glm::vec3 worldPoint)
{
    return glm::vec3(camera.getViewMatrix() * glm::vec4(worldPoint, 1.f));
}

} // namespace

TEST_CASE("A viewport camera starts looking at the origin")
{
    ViewportCamera camera;
    const glm::vec3 origin = toViewSpace(camera, glm::vec3(0.f));

    REQUIRE(origin.x == Approx(0.f).margin(1e-5));
    REQUIRE(origin.y == Approx(0.f).margin(1e-5));
    REQUIRE(origin.z < 0.f);
}

TEST_CASE("A viewport camera keeps its distance from the centre while orbiting")
{
    ViewportCamera camera;
    const float distanceBefore = glm::length(camera.getPosition());

    camera.orbit({120.f, -40.f});

    REQUIRE(glm::length(camera.getPosition()) == Approx(distanceBefore));
    REQUIRE(toViewSpace(camera, glm::vec3(0.f)).x == Approx(0.f).margin(1e-5));
}

TEST_CASE("A viewport camera stops short of looking straight down")
{
    ViewportCamera camera;
    camera.orbit({0.f, 100000.f});

    const glm::vec3 position = camera.getPosition();
    const float horizontalDistance = glm::length(glm::vec2(position.x, position.z));
    REQUIRE(position.y > 0.f);
    REQUIRE(horizontalDistance > 0.01f);
}

TEST_CASE("A viewport camera moves the scene with the pointer when panning")
{
    ViewportCamera camera;
    const glm::vec3 originBefore = toViewSpace(camera, glm::vec3(0.f));

    // Drags right and down, in pixels with y growing downward.
    camera.pan({50.f, 30.f});
    const glm::vec3 originAfter = toViewSpace(camera, glm::vec3(0.f));

    REQUIRE(originAfter.x > originBefore.x);
    REQUIRE(originAfter.y < originBefore.y);
    REQUIRE(originAfter.z == Approx(originBefore.z));
}

TEST_CASE("A viewport camera dollies toward the centre")
{
    ViewportCamera camera;
    const float distanceBefore = glm::length(camera.getPosition());

    camera.dolly(-1.f);

    REQUIRE(glm::length(camera.getPosition()) < distanceBefore);
    REQUIRE(toViewSpace(camera, glm::vec3(0.f)).x == Approx(0.f).margin(1e-5));
}

TEST_CASE("A viewport camera carries the centre forward when dollying past it")
{
    ViewportCamera camera;
    const glm::vec3 start = camera.getPosition();
    const glm::vec3 forward = -glm::normalize(start);

    camera.dolly(-1000.f);
    const glm::vec3 position = camera.getPosition();

    // Travels along the original view line past the old centre at the origin.
    REQUIRE(glm::dot(position - start, forward) > glm::length(start));
    REQUIRE(glm::length(glm::cross(position - start, forward)) == Approx(0.f).margin(1e-3));

    // Keeps the origin behind the camera after passing it.
    REQUIRE(toViewSpace(camera, glm::vec3(0.f)).z > 0.f);
}

TEST_CASE("A viewport camera projects depth from 0 at the near plane to 1 at the far plane")
{
    ViewportCamera camera;
    const glm::mat4 projection = camera.getProjectionMatrix(1.f);

    const glm::vec4 nearPoint = projection * glm::vec4(0.f, 0.f, -0.1f, 1.f);
    const glm::vec4 farPoint = projection * glm::vec4(0.f, 0.f, -1000.f, 1.f);

    REQUIRE(nearPoint.z / nearPoint.w == Approx(0.f).margin(1e-5));
    REQUIRE(farPoint.z / farPoint.w == Approx(1.f));
}

namespace {

// Returns the clip space position of a world point, after the divide by w.
glm::vec3 toNdc(const ViewportCamera& camera, float aspect, glm::vec3 worldPoint)
{
    const glm::vec4 clip =
        camera.getProjectionMatrix(aspect) * camera.getViewMatrix() * glm::vec4(worldPoint, 1.f);
    return glm::vec3(clip) / clip.w;
}

} // namespace

TEST_CASE("A viewport camera frames a box so every corner is in view, in a wide and a tall view")
{
    const glm::vec3 minimum(10.f, -2.f, 4.f);
    const glm::vec3 maximum(16.f, 4.f, 9.f);

    for (const float aspect : {2.f, 0.5f})
    {
        ViewportCamera camera;
        camera.orbit({40.f, -25.f});
        camera.frameBounds(minimum, maximum, aspect);

        // The centre of the box sits in the middle of the view.
        const glm::vec3 middle = toNdc(camera, aspect, (minimum + maximum) * 0.5f);
        REQUIRE(middle.x == Approx(0.f).margin(1e-4));
        REQUIRE(middle.y == Approx(0.f).margin(1e-4));

        bool touchesEdge = false;
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::vec3 point(
                (corner & 1) ? maximum.x : minimum.x,
                (corner & 2) ? maximum.y : minimum.y,
                (corner & 4) ? maximum.z : minimum.z
            );
            const glm::vec3 ndc = toNdc(camera, aspect, point);
            REQUIRE(std::abs(ndc.x) <= 1.f);
            REQUIRE(std::abs(ndc.y) <= 1.f);
            REQUIRE(ndc.z >= 0.f);
            REQUIRE(ndc.z <= 1.f);
            touchesEdge = touchesEdge || std::abs(ndc.x) > 0.5f || std::abs(ndc.y) > 0.5f;
        }
        // The box fills a good part of the view rather than being framed from far away.
        REQUIRE(touchesEdge);
    }
}

TEST_CASE("A viewport camera keeps its angles when it frames a box")
{
    ViewportCamera framed;
    framed.orbit({30.f, 10.f});
    ViewportCamera reference = framed;

    framed.frameBounds(glm::vec3(-1.f), glm::vec3(1.f), 1.f);
    const glm::vec3 toCamera = glm::normalize(framed.getPosition() - glm::vec3(0.f));
    const glm::vec3 referenceToCamera = glm::normalize(reference.getPosition());
    REQUIRE(glm::dot(toCamera, referenceToCamera) == Approx(1.f).margin(1e-5));
}

TEST_CASE("A viewport camera frames a single point and a very large box within its limits")
{
    ViewportCamera camera;
    camera.frameBounds(glm::vec3(3.f), glm::vec3(3.f), 1.f);
    REQUIRE(glm::length(camera.getPosition() - glm::vec3(3.f)) >= 1.f);
    REQUIRE(toNdc(camera, 1.f, glm::vec3(3.f)).z < 1.f);

    camera.frameBounds(glm::vec3(-1.0e6f), glm::vec3(1.0e6f), 1.f);
    // The camera stops short of the far plane instead of leaving the box behind it.
    REQUIRE(glm::length(camera.getPosition()) < 1000.f);
}

TEST_CASE("A viewport camera goes back to where a new one starts on reset")
{
    ViewportCamera camera;
    camera.orbit({120.f, 30.f});
    camera.pan({40.f, -10.f});
    camera.dolly(5.f);
    REQUIRE_FALSE(camera == ViewportCamera());

    camera.reset();
    REQUIRE(camera == ViewportCamera());
}
