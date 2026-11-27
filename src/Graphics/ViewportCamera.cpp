#include "Graphics/ViewportCamera.h"
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace helix::gfx {

namespace {

constexpr glm::vec3 worldUp{0.f, 1.f, 0.f};

// Radians turned per pixel dragged.
constexpr float orbitSpeed = 0.01f;

// Stops short of straight up and down, where the view would flip.
const float maxPitch = glm::radians(89.f);

// World units moved per pixel at a distance of 1.
constexpr float panSpeed = 0.0015f;

// Fraction of the distance moved per unit of dolly.
constexpr float dollySpeed = 0.1f;

// The closest the camera gets to the centre.
constexpr float minDistance = 1.f;

// The share of the far plane the camera stays within when it frames a box, so the box is not
// clipped by the far plane.
constexpr float maxFramedDistance = 0.45f * 1000.f;

// The share of the narrower side of the view a framed box fills.
constexpr float frameFill = 0.8f;

const float fieldOfView = glm::radians(45.f);
constexpr float nearPlane = 0.1f;
constexpr float farPlane = 1000.f;

// Returns the factor a movement scales by, the distance but never less than 1.
float getDistanceScale(float distance) { return std::max(distance, 1.f); }

} // namespace

void ViewportCamera::orbit(glm::vec2 delta)
{
    yaw_ -= delta.x * orbitSpeed;
    pitch_ = std::clamp(pitch_ + delta.y * orbitSpeed, -maxPitch, maxPitch);
}

void ViewportCamera::setAngles(float yaw, float pitch)
{
    yaw_ = yaw;
    pitch_ = std::clamp(pitch, -maxPitch, maxPitch);
}

void ViewportCamera::pan(glm::vec2 delta)
{
    const glm::vec3 forward = getForward();
    const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    const glm::vec3 up = glm::cross(right, forward);

    // Moves the centre against the drag so the scene follows the pointer.
    const float speed = panSpeed * getDistanceScale(distance_);
    center_ += (up * delta.y - right * delta.x) * speed;
}

void ViewportCamera::dolly(float amount)
{
    const float step = amount * dollySpeed * getDistanceScale(distance_);
    const float targetDistance = distance_ + step;
    if (targetDistance >= minDistance)
    {
        distance_ = targetDistance;
        return;
    }

    // Moves the centre forward by the part of the step past the closest distance.
    center_ += getForward() * (minDistance - targetDistance);
    distance_ = minDistance;
}

void ViewportCamera::reset() { *this = ViewportCamera(); }

void ViewportCamera::frameBounds(glm::vec3 minimum, glm::vec3 maximum, float aspect)
{
    const glm::vec3 boxCenter = (minimum + maximum) * 0.5f;
    glm::vec3 half = (maximum - minimum) * 0.5f;
    // A box with no extent is framed like a unit sphere.
    if (!(glm::length(half) > 1e-4f)) half = glm::vec3(1.f);

    const glm::vec3 forward = getForward();
    const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    const glm::vec3 up = glm::cross(right, forward);

    const float tanY = std::tan(fieldOfView * 0.5f);
    const float tanX = tanY * std::max(aspect, 1e-3f);

    // The corners of the box as seen from the camera: across (x), up (y) and along the view (z).
    glm::vec3 corners[8];
    for (int corner = 0; corner < 8; ++corner)
    {
        const glm::vec3 offset(
            (corner & 1) ? half.x : -half.x, (corner & 2) ? half.y : -half.y, (corner & 4) ? half.z : -half.z
        );
        corners[corner] = glm::vec3(glm::dot(offset, right), glm::dot(offset, up), glm::dot(offset, forward));
    }

    // The distance at which the box, slid across the view by `slide`, fills frameFill of the
    // narrower side, held between the closest distance and a share of the far plane.
    auto fitDistance = [&](glm::vec2 slide)
    {
        float distance = 0.f;
        float deepest = 0.f;
        for (const glm::vec3& corner : corners)
        {
            const glm::vec3 c = corner - glm::vec3(slide, 0.f);
            distance = std::max(distance, std::abs(c.x) / (frameFill * tanX) - c.z);
            distance = std::max(distance, std::abs(c.y) / (frameFill * tanY) - c.z);
            deepest = std::max(deepest, std::abs(c.z));
        }
        // Keeps every corner well in front of the camera.
        return std::clamp(std::max(distance, deepest + 0.1f * glm::length(half)), minDistance, maxFramedDistance);
    };

    // Slides the box across the view until its projection is centred. The slide and the distance
    // depend a little on each other (a corner nearer the camera is larger), so a few rounds
    // settle them.
    glm::vec2 slide(0.f);
    for (int round = 0; round < 4; ++round)
    {
        const float distance = fitDistance(slide);
        // A box too large to fit even from the farthest distance is framed around its middle: it
        // fills the view whatever the slide.
        if (distance >= maxFramedDistance)
        {
            slide = glm::vec2(0.f);
            break;
        }
        glm::vec2 low(std::numeric_limits<float>::max());
        glm::vec2 high(std::numeric_limits<float>::lowest());
        for (const glm::vec3& corner : corners)
        {
            const glm::vec3 c = corner - glm::vec3(slide, 0.f);
            const glm::vec2 projected(c.x / ((distance + c.z) * tanX), c.y / ((distance + c.z) * tanY));
            low = glm::min(low, projected);
            high = glm::max(high, projected);
        }
        const glm::vec2 middle = (low + high) * 0.5f;
        slide += glm::vec2(middle.x * distance * tanX, middle.y * distance * tanY);
    }
    const float distance = fitDistance(slide);

    center_ = boxCenter + right * slide.x + up * slide.y;
    distance_ = distance;
}

void ViewportCamera::placeAt(const glm::mat4& transform)
{
    const glm::vec3 position{transform[3]};
    const glm::vec3 forward = glm::normalize(-glm::vec3(transform[2]));

    // Solves getForward for the yaw and pitch that give this direction.
    yaw_ = std::atan2(-forward.x, -forward.z);
    pitch_ = std::clamp(std::asin(-forward.y), -maxPitch, maxPitch);
    center_ = position + getForward() * distance_;
}

glm::vec3 ViewportCamera::getForward() const
{
    const glm::vec3 centerToCamera{
        std::cos(pitch_) * std::sin(yaw_),
        std::sin(pitch_),
        std::cos(pitch_) * std::cos(yaw_),
    };
    return -centerToCamera;
}

glm::vec3 ViewportCamera::getPosition() const { return center_ - getForward() * distance_; }

glm::mat4 ViewportCamera::getViewMatrix() const
{
    return glm::lookAtRH(getPosition(), center_, worldUp);
}

glm::mat4 ViewportCamera::getProjectionMatrix(float aspect) const
{
    return glm::perspectiveRH_ZO(fieldOfView, aspect, nearPlane, farPlane);
}

} // namespace helix::gfx
