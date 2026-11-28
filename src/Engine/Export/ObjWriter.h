#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>

namespace helix {
class NodePacket;
}

namespace helix::exporter {

/// @brief What a write of the geometry to an obj file put in the file.
struct ObjStats
{
    /// @brief The `v` records: every point of every mesh, the ones no face uses included.
    std::uint64_t points = 0;
    /// @brief The `f` records, one per closed face of three points or more.
    std::uint64_t faces = 0;
    /// @brief The `l` records, one per open face (a polyline).
    std::uint64_t lines = 0;
    /// @brief Whether `vn` records were written, and whether `vt` records were.
    bool hasNormals = false;
    bool hasUvs = false;
    /// @brief Why nothing was written, empty when the write is whole. A point whose position is not
    /// a finite number cannot be written to an obj file, so it refuses rather than writes a zero.
    std::string error;
};

/// @brief Writes the meshes of a packet as Wavefront obj text.
///
/// Positions come out as `v`, the normals of the vertices as `vn`, the `uv` vertex attribute as
/// `vt`, closed faces as `f` and open faces as `l`. A point no face uses is still a `v` record, so
/// geometry made of points alone survives. When the packet holds several meshes each one opens
/// with a `g` record of its path, and the indices keep counting on across them as the format
/// wants. Primitives that are not meshes (a camera) are left out. Numbers are written with the
/// classic locale and nine significant digits, enough to read a float back exactly.
ObjStats writeObj(const NodePacket& packet, std::ostream& out);

} // namespace helix::exporter
