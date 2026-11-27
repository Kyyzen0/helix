#pragma once
#include "Engine/Primitives/Material.h"
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <vector>

namespace helix {
class NodePacket;
}

namespace helix::gfx {

/// @brief The index lists that say how display vertices join up.
struct DisplayTopology
{
    /// @brief Three vertex indices per triangle of the closed faces.
    std::vector<std::uint32_t> triangleIndices;
    /// @brief Two vertex indices per edge around the closed faces.
    std::vector<std::uint32_t> edgeIndices;
    /// @brief Two vertex indices per segment of the open faces.
    std::vector<std::uint32_t> lineIndices;
};

/// @brief A run of the triangle indices that share one material.
struct MaterialBatch
{
    /// @brief The first index of the run in `DisplayTopology::triangleIndices`.
    std::uint32_t firstIndex = 0;
    /// @brief The number of indices in the run, a multiple of three.
    std::uint32_t indexCount = 0;
    /// @brief The position of the material in `DisplayGeometry::materials`, or -1 for triangles
    /// that have none.
    std::int32_t material = -1;

    bool operator==(const MaterialBatch&) const = default;
};

/// @brief The geometry a viewport draws, flattened from every primitive in a packet.
///
/// @note Display vertex n is the mesh vertex at offset n, counting on from the
/// vertices of the meshes before it.
struct DisplayGeometry
{
    DisplayTopology topology;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    /// @brief The texture coordinate of every vertex, (0, 0) for a mesh that has none. The origin
    /// is the bottom left of the picture (see geo::Image).
    std::vector<glm::vec2> uvs;
    /// @brief The materials the meshes use, each once (by pointer), in order of first use.
    std::vector<std::shared_ptr<const geo::Material>> materials;
    /// @brief Splits `topology.triangleIndices` into runs of one material each, in order, so that
    /// a run is drawn with one draw call. The triangles of a run keep their order. Empty when
    /// there is no triangle.
    std::vector<MaterialBatch> batches;
    /// @brief Work memory of buildDisplayGeometry: the material of each triangle before the
    /// triangles are grouped. Kept to spare an allocation per rebuild.
    std::vector<std::int32_t> triangleMaterialScratch;
    std::vector<std::uint32_t> triangleIndexScratch;
    /// @brief The positions of every mesh point.
    std::vector<glm::vec3> pointPositions;
    /// @brief The positions of the mesh points that belong to no face.
    std::vector<glm::vec3> soloPointPositions;
    /// @brief The world transform of each camera.
    std::vector<glm::mat4> cameraTransforms;
    /// @brief The path of each camera, in the same order as the transforms.
    std::vector<std::string> cameraPaths;
};

/**
 * @brief Fills the geometry with every mesh and camera in the packet, replacing what it held.
 *
 * @note Keeps the memory of the geometry's buffers, so refilling a geometry of a
 * similar size allocates nothing.
 */
void buildDisplayGeometry(DisplayGeometry& geometry, const NodePacket& packet);

} // namespace helix::gfx
