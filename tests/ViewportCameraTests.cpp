#include "Graphics/ViewportCamera.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/vec4.hpp>
#include <algorithm>
#include <vector>

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

namespace {

// The corners of a box.
std::vector<glm::vec3> cornersOf(glm::vec3 minimum, glm::vec3 maximum)
{
    std::vector<glm::vec3> corners;
    for (int corner = 0; corner < 8; ++corner)
    {
        corners.emplace_back(
            (corner & 1) ? maximum.x : minimum.x,
            (corner & 2) ? maximum.y : minimum.y,
            (corner & 4) ? maximum.z : minimum.z
        );
    }
    return corners;
}

// The extent of the projection of a box in normalised device coordinates, from -1 to 1 across.
struct ProjectedBox
{
    glm::vec2 low;
    glm::vec2 high;
    bool inFront = true;
};

ProjectedBox projectBox(const ViewportCamera& camera, float aspect, glm::vec3 minimum, glm::vec3 maximum)
{
    ProjectedBox box{glm::vec2(1.0e9f), glm::vec2(-1.0e9f), true};
    for (const glm::vec3& corner : cornersOf(minimum, maximum))
    {
        const glm::vec3 ndc = toNdc(camera, aspect, corner);
        box.inFront = box.inFront && ndc.z >= 0.f && ndc.z <= 1.f;
        box.low = glm::min(box.low, glm::vec2(ndc));
        box.high = glm::max(box.high, glm::vec2(ndc));
    }
    return box;
}

} // namespace

TEST_CASE("A viewport camera frames the projected box to fill 80% of the narrower side")
{
    const glm::vec3 minimum(10.f, -2.f, 4.f);
    const glm::vec3 maximum(16.f, 4.f, 9.f);

    for (const float aspect : {2.f, 0.5f})
    {
        ViewportCamera camera;
        camera.orbit({40.f, -25.f});
        camera.frameBounds(minimum, maximum, aspect);

        const ProjectedBox box = projectBox(camera, aspect, minimum, maximum);
        REQUIRE(box.inFront);

        // The projection is centred in the view.
        REQUIRE(((box.low.x + box.high.x) * 0.5f) == Approx(0.f).margin(0.02));
        REQUIRE(((box.low.y + box.high.y) * 0.5f) == Approx(0.f).margin(0.02));

        // It is whole in the view, and the side that limits it fills 80% of the view across.
        const float width = box.high.x - box.low.x;
        const float height = box.high.y - box.low.y;
        REQUIRE(width <= 2.f * 0.8f + 0.02f);
        REQUIRE(height <= 2.f * 0.8f + 0.02f);
        REQUIRE(std::max(width, height) == Approx(2.f * 0.8f).margin(0.03));
    }
}

TEST_CASE("A viewport camera frames a long thin box much closer than its bounding sphere would")
{
    // The shape of a dragon with its tail stretched out: long along x, small in the other two.
    const glm::vec3 minimum(-10.f, 0.f, -1.f);
    const glm::vec3 maximum(10.f, 3.f, 1.f);

    // Seen from the side, the long axis spans the view.
    ViewportCamera camera;
    camera.orbit({0.f, 0.f});
    camera.frameBounds(minimum, maximum, 4.f / 3.f);
    const ProjectedBox box = projectBox(camera, 4.f / 3.f, minimum, maximum);
    REQUIRE(box.inFront);
    REQUIRE(std::max(box.high.x - box.low.x, box.high.y - box.low.y) > 1.2f);

    // The bounding sphere would have put the camera at radius / sin(half angle), more than 1.15
    // times that distance: the box is framed from nearer than the sphere's fit.
    const float radius = 0.5f * glm::length(maximum - minimum);
    const float sphereDistance = radius / std::sin(glm::radians(22.5f));
    REQUIRE(glm::length(camera.getPosition() - (minimum + maximum) * 0.5f) < sphereDistance * 0.75f);
}

TEST_CASE("A viewport camera keeps its angles when it frames a box")
{
    ViewportCamera framed;
    framed.orbit({30.f, 10.f});
    ViewportCamera reference = framed;

    framed.frameBounds(glm::vec3(-1.f), glm::vec3(1.f), 1.f);

    // The camera looks along the same direction. Only its centre and its distance changed.
    const glm::vec3 forward = glm::normalize(-glm::vec3(framed.getViewMatrix()[0][2], framed.getViewMatrix()[1][2], framed.getViewMatrix()[2][2]));
    const glm::vec3 referenceForward = glm::normalize(-glm::vec3(reference.getViewMatrix()[0][2], reference.getViewMatrix()[1][2], reference.getViewMatrix()[2][2]));
    REQUIRE(glm::dot(forward, referenceForward) == Approx(1.f).margin(1e-5));
}

TEST_CASE("A viewport camera frames a box that is already centred and a box that fills the view")
{
    // A cube at the origin, seen from the default angles: the camera keeps looking near its
    // middle, up to the shift that centres the projection of its corners (the near ones are
    // larger), which is a fraction of the half size of the cube.
    ViewportCamera camera;
    camera.frameBounds(glm::vec3(-1.f), glm::vec3(1.f), 4.f / 3.f);
    REQUIRE(glm::length(toViewSpace(camera, glm::vec3(0.f)) * glm::vec3(1.f, 1.f, 0.f)) < 0.5f);

    // A slab as wide as the view and as thin as a sheet (a ground plane, a background).
    const glm::vec3 minimum(-50.f, -0.01f, -50.f);
    const glm::vec3 maximum(50.f, 0.01f, 50.f);
    camera.frameBounds(minimum, maximum, 16.f / 9.f);
    const ProjectedBox box = projectBox(camera, 16.f / 9.f, minimum, maximum);
    REQUIRE(box.inFront);
    REQUIRE(box.high.x - box.low.x <= 2.f * 0.8f + 0.02f);
    REQUIRE(box.high.y - box.low.y <= 2.f * 0.8f + 0.02f);
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
