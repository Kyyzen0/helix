#include "Graphics/DisplayGeometry.h"
#include "Engine/GeometryAlgorithms/MeshUtils.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Primitives/Camera.h"
#include "Engine/Attribute/AttributeHandle.h"
#include "Engine/Attribute/AttributeNames.h"
#include "Engine/Primitives/Mesh.h"
#include <optional>
#include <span>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <algorithm>

namespace helix::gfx {

namespace {

glm::vec3 toGlm(const Vector3& vector)
{
    return glm::vec3(float(vector.x()), float(vector.y()), float(vector.z()));
}

glm::mat4 toGlm(const Matrix4& matrix)
{
    glm::mat4 converted;
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
            converted[column][row] = float(matrix(row, column));
    }
    return converted;
}

/// @brief Returns where a material is in the table of the geometry, adding it when it is new.
std::int32_t materialSlot(DisplayGeometry& geometry, const std::shared_ptr<const geo::Material>& material)
{
    if (!material) return -1;
    const auto found = std::find(geometry.materials.begin(), geometry.materials.end(), material);
    if (found != geometry.materials.end()) return std::int32_t(found - geometry.materials.begin());
    geometry.materials.push_back(material);
    return std::int32_t(geometry.materials.size() - 1);
}

/// @brief Appends the index lists of one mesh, numbered from its first display vertex, and the
/// material of each triangle it adds.
void appendTopology(DisplayGeometry& geometry, const geo::Mesh& mesh, std::uint32_t firstVertex)
{
    DisplayTopology& topology = geometry.topology;
    std::vector<Offset> closedFaceOffsets;
    closedFaceOffsets.reserve(mesh.getNumFaces());
    topology.edgeIndices.reserve(topology.edgeIndices.size() + 2 * mesh.getNumVerts());
    const std::span<const Offset> faceStarts = mesh.getFaceStartVertices();
    for (Offset faceOffset = 0; faceOffset < mesh.getNumFaces(); ++faceOffset)
    {
        if (!mesh.isValidFace(faceOffset)) continue;

        const std::uint32_t faceVertexCount = mesh.getFaceVertCount(faceOffset);
        const std::uint32_t startVertex = firstVertex + std::uint32_t(faceStarts[faceOffset]);
        const bool closed = mesh.isClosed(faceOffset);

        // Joins the vertices of an open face into line segments.
        if (!closed && faceVertexCount >= 2)
        {
            for (std::uint32_t corner = 0; corner + 1 < faceVertexCount; ++corner)
            {
                topology.lineIndices.push_back(startVertex + corner);
                topology.lineIndices.push_back(startVertex + corner + 1);
            }
        }
        else if (faceVertexCount >= 3)
        {
            closedFaceOffsets.push_back(faceOffset);
            for (std::uint32_t corner = 0; corner < faceVertexCount; ++corner)
            {
                topology.edgeIndices.push_back(startVertex + corner);
                topology.edgeIndices.push_back(startVertex + (corner + 1) % faceVertexCount);
            }
        }
    }

    // Ear clips the closed faces so concave faces fill correctly.
    const std::vector<std::array<Offset, 3>> triangles =
        utils::earClipTriangleIndices(mesh, closedFaceOffsets);
    const std::size_t firstTriangleIndex = topology.triangleIndices.size();

    // The material of each face, as a position in the table of the geometry.
    std::vector<std::int32_t>& triangleMaterials = geometry.triangleMaterialScratch;
    const std::size_t firstTriangle = firstTriangleIndex / 3;
    triangleMaterials.resize(firstTriangle + triangles.size(), -1);
    const std::vector<std::shared_ptr<const geo::Material>>& meshMaterials = mesh.materials();
    const std::shared_ptr<const attr::Attribute> materialIndices =
        meshMaterials.empty() ? nullptr : mesh.getAttribByName(attr::AttrOwner::FACE, attr::names::materialIndex);
    if (materialIndices && materialIndices->getType() == attr::AttributeType::intT)
    {
        const attr::AttributeHandleRO<intT> faceMaterials(materialIndices);
        const std::size_t faceCount = faceMaterials.getSize();
        std::vector<std::int32_t> slots(meshMaterials.size());
        for (std::size_t material = 0; material < meshMaterials.size(); ++material)
            slots[material] = materialSlot(geometry, meshMaterials[material]);

        std::size_t triangle = firstTriangle;
        for (const Offset faceOffset : closedFaceOffsets)
        {
            const std::uint32_t faceTriangles = mesh.getFaceVertCount(faceOffset) - 2;
            std::int32_t slot = -1;
            if (faceOffset < faceCount)
            {
                const intT index = faceMaterials[faceOffset];
                if (index >= 0 && std::size_t(index) < slots.size()) slot = slots[std::size_t(index)];
            }
            for (std::uint32_t i = 0; i < faceTriangles; ++i)
                triangleMaterials[triangle++] = slot;
        }
    }
    topology.triangleIndices.resize(firstTriangleIndex + 3 * triangles.size());
    tbb::parallel_for(
        tbb::blocked_range<std::size_t>(0, triangles.size()),
        [&](const tbb::blocked_range<std::size_t>& range) {
            for (std::size_t triangleIndex = range.begin(); triangleIndex < range.end(); ++triangleIndex)
            {
                const std::size_t indexSlot = firstTriangleIndex + 3 * triangleIndex;
                for (std::size_t corner = 0; corner < 3; ++corner)
                    topology.triangleIndices[indexSlot + corner] =
                        firstVertex + std::uint32_t(triangles[triangleIndex][corner]);
            }
        }
    );
}

/// @brief Writes the position, normal and texture coordinate of every vertex of one mesh, from its first display vertex on.
void writeVertices(DisplayGeometry& geometry, const geo::Mesh& mesh, std::uint32_t firstVertex)
{
    const std::span<const intT> vertexPoints = mesh.vertexPointSpan();
    const std::span<const Vector3> pointPositions = mesh.pointPosSpan();
    const geo::VertexNormalHandle vertexNormals = mesh.getVertexNormal();
    const std::shared_ptr<const attr::Attribute> uvAttribute =
        mesh.getAttribByName(attr::AttrOwner::VERTEX, attr::names::uv);
    std::optional<attr::AttributeHandleRO<Vector3>> uvHandle;
    if (uvAttribute && uvAttribute->getType() == attr::AttributeType::vectorT) uvHandle.emplace(uvAttribute);
    const std::size_t uvCount = uvHandle ? uvHandle->getSize() : 0;

    tbb::parallel_for(
        tbb::blocked_range<Offset>(0, mesh.getNumVerts()),
        [&](const tbb::blocked_range<Offset>& range) {
            for (Offset vertexOffset = range.begin(); vertexOffset < range.end(); ++vertexOffset)
            {
                const std::uint32_t displayVertex = firstVertex + std::uint32_t(vertexOffset);
                geometry.positions[displayVertex] = toGlm(pointPositions[vertexPoints[vertexOffset]]);
                geometry.normals[displayVertex] = toGlm(vertexNormals[vertexOffset]);
                geometry.uvs[displayVertex] =
                    vertexOffset < uvCount ? glm::vec2(float((*uvHandle)[vertexOffset].x()), float((*uvHandle)[vertexOffset].y()))
                                           : glm::vec2(0.f);
            }
        }
    );
}

/// @brief Appends the positions of every point of one mesh.
void appendPoints(std::vector<glm::vec3>& pointPositions, const geo::Mesh& mesh)
{
    const std::span<const Vector3> positions = mesh.pointPosSpan();
    pointPositions.reserve(pointPositions.size() + positions.size());
    for (Offset pointOffset = 0; pointOffset < positions.size(); ++pointOffset)
    {
        if (!mesh.isValidPoint(pointOffset)) continue;
        pointPositions.push_back(toGlm(positions[pointOffset]));
    }
}

/// @brief Appends the positions of the points of one mesh that belong to no face.
void appendSoloPoints(std::vector<glm::vec3>& soloPointPositions, const geo::Mesh& mesh)
{
    for (auto pointIt = mesh.soloPointsBegin(); pointIt != mesh.soloPointsEnd(); ++pointIt)
        soloPointPositions.push_back(toGlm(mesh.getPointPos(*pointIt)));
}

/// @brief Fills `batches` with one run per material, grouping the triangle indices to match.
void groupByMaterial(DisplayGeometry& geometry)
{
    DisplayTopology& topology = geometry.topology;
    const std::vector<std::int32_t>& triangleMaterials = geometry.triangleMaterialScratch;
    geometry.batches.clear();
    const std::size_t triangleCount = topology.triangleIndices.size() / 3;
    if (triangleCount == 0) return;

    const bool anyMaterial = !geometry.materials.empty();
    if (!anyMaterial)
    {
        geometry.batches.push_back({0, std::uint32_t(topology.triangleIndices.size()), -1});
        return;
    }

    // A stable counting sort by material; the triangles with none come first.
    const std::size_t slotCount = geometry.materials.size() + 1;
    std::vector<std::size_t> starts(slotCount + 1, 0);
    for (std::size_t triangle = 0; triangle < triangleCount; ++triangle)
        ++starts[std::size_t(triangleMaterials[triangle] + 1) + 1];
    for (std::size_t slot = 0; slot < slotCount; ++slot)
        starts[slot + 1] += starts[slot];

    std::vector<std::uint32_t>& sorted = geometry.triangleIndexScratch;
    sorted.resize(topology.triangleIndices.size());
    std::vector<std::size_t> next(starts.begin(), starts.end() - 1);
    for (std::size_t triangle = 0; triangle < triangleCount; ++triangle)
    {
        const std::size_t destination = next[std::size_t(triangleMaterials[triangle] + 1)]++;
        std::copy_n(&topology.triangleIndices[3 * triangle], 3, &sorted[3 * destination]);
    }
    topology.triangleIndices.swap(sorted);

    for (std::size_t slot = 0; slot < slotCount; ++slot)
    {
        if (starts[slot + 1] == starts[slot]) continue;
        geometry.batches.push_back(
            {std::uint32_t(3 * starts[slot]), std::uint32_t(3 * (starts[slot + 1] - starts[slot])), std::int32_t(slot) - 1}
        );
    }
}

} // namespace

void buildDisplayGeometry(DisplayGeometry& geometry, const NodePacket& packet)
{
    const std::vector<geo::PrimPtr> meshPrims = packet.getPrimitives(geo::PrimType::MESH);

    std::size_t vertexCount = 0;
    for (const geo::PrimPtr& prim : meshPrims)
        vertexCount += std::static_pointer_cast<const geo::Mesh>(prim)->getNumVerts();

    // Empties the lists the meshes append to, which keeps their memory.
    geometry.topology.triangleIndices.clear();
    geometry.topology.edgeIndices.clear();
    geometry.topology.lineIndices.clear();
    geometry.materials.clear();
    geometry.batches.clear();
    geometry.triangleMaterialScratch.clear();
    geometry.pointPositions.clear();
    geometry.soloPointPositions.clear();
    geometry.cameraTransforms.clear();
    geometry.cameraPaths.clear();
    geometry.positions.resize(vertexCount);
    geometry.normals.resize(vertexCount);
    geometry.uvs.resize(vertexCount);

    std::uint32_t firstVertex = 0;
    for (const geo::PrimPtr& prim : meshPrims)
    {
        const auto mesh = std::static_pointer_cast<const geo::Mesh>(prim);
        appendTopology(geometry, *mesh, firstVertex);
        writeVertices(geometry, *mesh, firstVertex);
        appendPoints(geometry.pointPositions, *mesh);
        appendSoloPoints(geometry.soloPointPositions, *mesh);
        firstVertex += std::uint32_t(mesh->getNumVerts());
    }

    groupByMaterial(geometry);

    for (const geo::PrimPtr& prim : packet.getPrimitives(geo::PrimType::CAMERA))
    {
        const auto camera = std::static_pointer_cast<const geo::Camera>(prim);
        geometry.cameraTransforms.push_back(toGlm(camera->getTransform()));
        geometry.cameraPaths.push_back(camera->getPath());
    }
}

} // namespace helix::gfx
