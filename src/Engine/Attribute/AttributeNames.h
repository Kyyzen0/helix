#pragma once

namespace helix::attr {

/**
 * @brief Names of the attributes the engine gives a meaning to.
 */
namespace names {

/// The point position every mesh is created with.
inline constexpr const char* position = "Position";
/// The direction a face, point or vertex faces.
inline constexpr const char* normal = "Normal";
/// The direction that orients a point's up axis.
inline constexpr const char* up = "Up";
/// The first set of texture coordinates of an imported mesh, one per vertex. A vector whose z is
/// zero, since the engine has no two component attribute.
inline constexpr const char* uv = "uv";
/// The colour of a vertex, as the file stored it.
inline constexpr const char* color = "Cd";
/// The material of a face, as an index into the materials the import found (-1 for none).
inline constexpr const char* materialIndex = "material_index";

/// How many joints influence a point of a skinned mesh.
inline constexpr int skinInfluenceCount = 4;
/// The joint of each influence of a point (an int, the index of a point of the skeleton), and its
/// weight (a float). The weights of a point sum to 1, and an unused influence is joint 0 with
/// weight 0. A point whose weights are all 0 is not moved by a skin.
inline constexpr const char* skinJoint[skinInfluenceCount] = {
    "skin_joint0", "skin_joint1", "skin_joint2", "skin_joint3"
};
inline constexpr const char* skinWeight[skinInfluenceCount] = {
    "skin_weight0", "skin_weight1", "skin_weight2", "skin_weight3"
};

/// Point attributes of a skeleton, a mesh whose points are the joints.
/// The point of the parent joint, or -1 for a root.
inline constexpr const char* jointParent = "joint_parent";
/// The transform of the joint at rest, relative to its parent (a matrix).
inline constexpr const char* restLocal = "rest_local";
/// The transform that takes a point of the skinned mesh to the space of the joint at rest, so the
/// skin matrix of a joint is pose * inverse_bind (a matrix).
inline constexpr const char* inverseBind = "inverse_bind";
/// The current transform of the joint in the space of the mesh: its rest world transform until a
/// skeletonPose node poses it (a matrix).
inline constexpr const char* pose = "pose";

} // namespace names

} // namespace helix::attr
