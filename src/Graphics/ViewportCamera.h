#pragma once
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace helix::gfx {

/// @brief The camera a viewport looks through, orbiting a centre point.
class ViewportCamera
{
  public:
    /// @brief Turns the camera around the centre by a pointer drag in pixels.
    void orbit(glm::vec2 delta);

    /// @brief Slides the camera and its centre across the view by a pointer drag in pixels.
    void pan(glm::vec2 delta);

    /// @brief Sets the angles the camera looks from, in radians, keeping the centre and the
    /// distance. The pitch is held short of straight up and down, like @ref orbit does.
    void setAngles(float yaw, float pitch);

    /// @brief Moves the camera toward the centre for a negative amount and away for a positive one.
    ///
    /// @note Close to the centre the camera keeps moving forward and carries the centre with it.
    void dolly(float amount);

    /// @brief Puts the camera back where a new viewport starts: looking at the origin from a
    /// distance of 15, at the default angles.
    void reset();

    /// @brief Moves the centre and the distance so a box fills the view, keeping the angles.
    ///
    /// The box is fitted by its projection at the current angles, not by its bounding sphere,
    /// which a long thin shape (a dragon with a tail) makes far larger than the shape: the
    /// corners of the box are projected, centred in the view, and the camera backs off until
    /// they fill 80% of the narrower side of the view. Orbiting afterwards can turn a corner
    /// outside the view. A box with no extent (a single point) is framed like a unit sphere.
    ///
    /// @param minimum the corner of the box with the smallest coordinates.
    /// @param maximum the corner of the box with the largest coordinates.
    /// @param aspect the view width divided by its height.
    /// @note The distance is held between the closest distance of `dolly` and a share of the far
    /// plane, so a huge box is framed as large as the view can show it, not clipped away.
    void frameBounds(glm::vec3 minimum, glm::vec3 maximum, float aspect);

    /// @brief Moves the camera to a transform, looking down its negative z axis with the centre at
    /// the current distance.
    ///
    /// @note The camera stays level, so any roll in the transform is dropped.
    void placeAt(const glm::mat4& transform);

    /// @brief Returns the angle around the vertical axis, in radians (see @ref setAngles).
    float getYaw() const { return yaw_; }

    /// @brief Returns the angle above the horizontal plane, in radians.
    float getPitch() const { return pitch_; }

    /// @brief Returns the distance from the camera to its centre.
    float getDistance() const { return distance_; }

    /// @brief Returns the point the camera looks at.
    glm::vec3 getCenter() const { return center_; }

    /// @brief Returns the camera position in world space.
    glm::vec3 getPosition() const;

    /// @brief Returns the matrix taking world space to view space.
    glm::mat4 getViewMatrix() const;

    /// @brief Returns the matrix taking view space to clip space, with depth from 0 to 1.
    ///
    /// @param aspect the view width divided by its height.
    glm::mat4 getProjectionMatrix(float aspect) const;

    bool operator==(const ViewportCamera&) const = default;

  private:
    /// @brief Returns the unit direction the camera looks along, from its position to the centre.
    glm::vec3 getForward() const;

    glm::vec3 center_{0.f, 0.f, 0.f};
    float distance_ = 15.f;
    /// @brief The angle around the vertical axis, 0 looking down negative z. A new camera sits at
    /// +45: on the front-right of the model, so the home view shows its front (+z), its right (+x)
    /// and its top. It used to sit at -135, which looks at the model from behind.
    float yaw_ = glm::radians(45.f);
    /// @brief The angle above the horizontal plane.
    float pitch_ = glm::radians(25.f);
};

} // namespace helix::gfx
