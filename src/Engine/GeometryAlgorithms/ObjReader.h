#pragma once
#include <istream>

namespace helix::geo {
class Mesh;
}

namespace helix::utils {

/**
 * @brief Reads the vertex and face records of an obj file into a mesh.
 *
 * A "f" record closes its face and an "l" record leaves it open.
 *
 * @param readTexcoords When true, the "vt" records the faces name are stored as a vertex attribute
 * called "uv" (a vector whose z is zero), when the file has any. Off by default, which leaves the
 * mesh exactly as it has always been.
 *
 * @note Records the mesh has nowhere to put, such as materials and normals, are skipped. The
 * normals are skipped on purpose: a mesh from an obj file has always had its normals computed.
 */
void readObjInto(std::istream& file, geo::Mesh& mesh, bool readTexcoords = false);

} // namespace helix::utils
