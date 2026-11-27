#pragma once
#include "Engine/Core/Types.h"
#include <Eigen/Geometry>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace helix::geo {

/// @brief A rotation, stored as a unit quaternion.
using Quaternion = Eigen::Quaternionf;

/// @brief How a track moves between two keys.
enum class Interpolation
{
    /// @brief The value of the last key passed holds until the next one.
    Step,
    /// @brief A straight line between keys (the shortest arc for a rotation).
    Linear
};

/**
 * @brief The keys of one animated value of one joint.
 *
 * @note A track with no key says nothing: the joint keeps its rest value.
 */
template <typename T> struct ClipTrack
{
    /// @brief The time of each key in seconds, ascending.
    std::vector<float> times;
    /// @brief The value at each key, `times.size()` of them.
    std::vector<T> values;
    Interpolation interpolation = Interpolation::Linear;

    bool empty() const { return times.empty() || values.size() != times.size(); }
};

/// @brief What a clip says about one joint: its local translation, rotation and scale over time.
struct JointTracks
{
    ClipTrack<Vector3> translation;
    ClipTrack<Quaternion> rotation;
    ClipTrack<Vector3> scale;

    bool any() const { return !translation.empty() || !rotation.empty() || !scale.empty(); }
};

/**
 * @brief An animation of a skeleton, as a pure function of time.
 *
 * Immutable once built and shared through `std::shared_ptr<const AnimationClip>` by every copy of
 * the skeleton that carries it.
 */
struct AnimationClip
{
    std::string name;
    /// @brief The time of the last key of any track, in seconds.
    float duration = 0.f;
    /// @brief One entry per joint of the skeleton, in joint order. A joint the clip does not
    /// animate has empty tracks.
    std::vector<JointTracks> joints;

    /// @brief How many tracks (translation, rotation or scale of a joint) hold keys.
    std::size_t channelCount() const;
};

/**
 * @brief What a skeleton carries besides its points, shared by every copy of the mesh.
 *
 * A skeleton is a mesh whose points are the joints, so the transforms (`rest_local`, `inverse_bind`,
 * `pose`) live in point attributes where every node can read them. What does not fit a point
 * attribute lives here: the names, the clips, and the transform above each root joint.
 *
 * Immutable, shared by pointer like the materials of a mesh, and not saved in a scene: the import
 * node reads it again from the file.
 */
struct Skeleton
{
    /// @brief The name of each joint, in joint order (the order of the points).
    std::vector<std::string> jointNames;
    /// @brief For each joint, the transform of everything above it that is not a joint (the
    /// armature node of a glTF, the correction of the up axis). Identity for a joint that has a
    /// parent joint, and used only by a root.
    std::vector<Matrix4> rootBase;
    std::vector<std::shared_ptr<const AnimationClip>> clips;
    /// @brief The factor that turns the lengths of the file into the lengths of the skeleton: the
    /// import's size. It applies to what the table holds (the translations of the clips and the
    /// base of the roots), since the point attributes were scaled when it was set.
    float scale = 1.f;

    /// @brief The clip of this name, or null.
    const AnimationClip* findClip(std::string_view name) const;
};

/// @brief The value of a translation or scale track at a time in seconds. A time before the first
/// key gives the first value, and one past the last gives the last. An empty track gives zero.
Vector3 sampleTrack(const ClipTrack<Vector3>& track, float time);

/// @brief The value of a rotation track at a time in seconds, with the same limits as the vector
/// version. A linear track takes the shortest arc between keys. An empty track gives the identity.
Quaternion sampleTrack(const ClipTrack<Quaternion>& track, float time);

/**
 * @brief Splits a matrix into translation, rotation and scale, assuming it holds no shear.
 *
 * A negative determinant (a mirror) is carried by the sign of the x scale, so recomposing gives
 * the matrix back.
 */
void decomposeTrs(const Matrix4& matrix, Vector3& translation, Quaternion& rotation, Vector3& scale);

/// @brief Builds the matrix of translation * rotation * scale.
Matrix4 composeTrs(const Vector3& translation, const Quaternion& rotation, const Vector3& scale);

} // namespace helix::geo
