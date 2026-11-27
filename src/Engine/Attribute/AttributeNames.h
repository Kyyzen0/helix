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

} // namespace names

} // namespace helix::attr
