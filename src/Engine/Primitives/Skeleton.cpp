#include "Engine/Primitives/Skeleton.h"
#include <algorithm>
#include <cmath>

namespace helix::geo {

std::size_t AnimationClip::channelCount() const
{
    std::size_t count = 0;
    for (const JointTracks& joint : joints)
        count += std::size_t(!joint.translation.empty()) + std::size_t(!joint.rotation.empty()) +
                 std::size_t(!joint.scale.empty());
    return count;
}

const AnimationClip* Skeleton::findClip(std::string_view name) const
{
    for (const std::shared_ptr<const AnimationClip>& clip : clips)
        if (clip && clip->name == name) return clip.get();
    return nullptr;
}

namespace {

// The key a time falls after, and how far between it and the next one (0 to 1). A time outside
// the keys has no next key: it holds the first or the last value.
struct KeySpan
{
    std::size_t first = 0;
    std::size_t second = 0;
    float blend = 0.f;
};

KeySpan locate(const std::vector<float>& times, float time)
{
    // The first key strictly after the time. Two keys at the same time make a jump, and the later
    // one wins from that time on.
    const auto after = std::upper_bound(times.begin(), times.end(), time);
    if (after == times.begin()) return {0, 0, 0.f};
    if (after == times.end()) return {times.size() - 1, times.size() - 1, 0.f};
    const std::size_t second = static_cast<std::size_t>(after - times.begin());
    const std::size_t first = second - 1;
    const float length = times[second] - times[first];
    return {first, second, length > 0.f ? (time - times[first]) / length : 0.f};
}

} // namespace

Vector3 sampleTrack(const ClipTrack<Vector3>& track, float time)
{
    if (track.empty()) return Vector3::Zero();
    const KeySpan span = locate(track.times, time);
    if (track.interpolation == Interpolation::Step || span.first == span.second)
        return track.values[span.first];
    return track.values[span.first] + (track.values[span.second] - track.values[span.first]) * span.blend;
}

Quaternion sampleTrack(const ClipTrack<Quaternion>& track, float time)
{
    if (track.empty()) return Quaternion::Identity();
    const KeySpan span = locate(track.times, time);
    if (track.interpolation == Interpolation::Step || span.first == span.second)
        return track.values[span.first].normalized();
    // slerp turns the short way round whichever sign the two keys happen to carry.
    return track.values[span.first].normalized().slerp(span.blend, track.values[span.second].normalized()).normalized();
}

void decomposeTrs(const Matrix4& matrix, Vector3& translation, Quaternion& rotation, Vector3& scale)
{
    translation = matrix.col(3).head<3>();
    Matrix3 linear = matrix.topLeftCorner<3, 3>();
    scale = Vector3(linear.col(0).norm(), linear.col(1).norm(), linear.col(2).norm());
    if (linear.determinant() < 0.f) scale.x() = -scale.x();
    for (int axis = 0; axis < 3; ++axis)
    {
        // A flattened axis has no direction to keep: it becomes the identity one.
        if (std::abs(scale[axis]) > 1e-12f)
            linear.col(axis) /= scale[axis];
        else
            linear.col(axis) = Matrix3::Identity().col(axis);
    }
    rotation = Quaternion(linear).normalized();
}

Matrix4 composeTrs(const Vector3& translation, const Quaternion& rotation, const Vector3& scale)
{
    Matrix4 matrix = Matrix4::Identity();
    matrix.topLeftCorner<3, 3>() = rotation.normalized().toRotationMatrix() * scale.asDiagonal();
    matrix.col(3).head<3>() = translation;
    return matrix;
}

} // namespace helix::geo
