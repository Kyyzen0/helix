#include "Engine/Attribute/AttributeHandle.h"
#include "Engine/Attribute/AttributeNames.h"
#include "Engine/Core/Types.h"
#include "Engine/Import/ImportFile.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Primitives/Camera.h"
#include "Engine/Primitives/Mesh.h"
#include "Graphics/DisplayGeometry.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include <vector>

using namespace helix;
using namespace helix::gfx;

namespace {

// Returns a mesh with one closed square face on the ground plane.
std::shared_ptr<geo::Mesh> buildSquareMesh(std::string_view path)
{
    auto mesh = std::make_shared<geo::Mesh>(path);
    const std::vector<Vector3> positions = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
    const std::vector<Offset> pointOffsets = mesh->addPoints(positions);
    mesh->addFace(pointOffsets);
    return mesh;
}

} // namespace

TEST_CASE("Display geometry splits a closed face into triangles with an outline")
{
    NodePacket packet;
    packet.addPrimitive(buildSquareMesh("/square"));

    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, packet);

    REQUIRE(geometry.positions.size() == 4);
    REQUIRE(geometry.normals.size() == 4);
    REQUIRE(geometry.topology.triangleIndices.size() == 6);
    REQUIRE(geometry.topology.edgeIndices.size() == 8);
    REQUIRE(geometry.topology.lineIndices.empty());
}

TEST_CASE("Display geometry draws an open face as a line")
{
    auto mesh = std::make_shared<geo::Mesh>("/line");
    const std::vector<Vector3> positions = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    mesh->addFace(mesh->addPoints(positions), false);
    NodePacket packet;
    packet.addPrimitive(mesh);

    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, packet);

    REQUIRE(geometry.topology.lineIndices == std::vector<std::uint32_t>{0, 1, 1, 2});
    REQUIRE(geometry.topology.triangleIndices.empty());
    REQUIRE(geometry.topology.edgeIndices.empty());
}

TEST_CASE("Display geometry numbers each mesh on from the vertices before it")
{
    NodePacket packet;
    packet.addPrimitive(buildSquareMesh("/first"));
    packet.addPrimitive(buildSquareMesh("/second"));

    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, packet);

    REQUIRE(geometry.positions.size() == 8);
    const std::vector<std::uint32_t>& triangleIndices = geometry.topology.triangleIndices;
    REQUIRE(triangleIndices.size() == 12);
    for (std::size_t corner = 0; corner < 6; ++corner)
    {
        REQUIRE(triangleIndices[corner] < 4);
        REQUIRE(triangleIndices[corner + 6] >= 4);
    }
}

TEST_CASE("Display geometry reads each vertex position from its point")
{
    NodePacket packet;
    packet.addPrimitive(buildSquareMesh("/square"));

    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, packet);

    REQUIRE(geometry.positions[2] == glm::vec3(1.f, 0.f, 1.f));
    REQUIRE(geometry.normals[0].y != 0.f);
}

TEST_CASE("Display geometry leaves out deleted faces")
{
    std::shared_ptr<geo::Mesh> mesh = buildSquareMesh("/squares");
    const std::vector<Vector3> positions = {{2, 0, 0}, {3, 0, 0}, {3, 0, 1}, {2, 0, 1}};
    mesh->addFace(mesh->addPoints(positions));
    mesh->deleteFaces({0}, false);
    NodePacket packet;
    packet.addPrimitive(mesh);

    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, packet);

    REQUIRE(geometry.topology.triangleIndices.size() == 6);
    REQUIRE(geometry.topology.edgeIndices.size() == 8);
}

TEST_CASE("Display geometry keeps the points that belong to no face")
{
    std::shared_ptr<geo::Mesh> mesh = buildSquareMesh("/square");
    mesh->addPoint({5, 0, 0});
    NodePacket packet;
    packet.addPrimitive(mesh);

    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, packet);

    REQUIRE(geometry.soloPointPositions == std::vector<glm::vec3>{{5.f, 0.f, 0.f}});
}

TEST_CASE("Display geometry places each camera by its transform")
{
    auto camera = std::make_shared<geo::Camera>("/camera");
    Matrix4 transform = Matrix4::Identity();
    transform(0, 3) = 2.f;
    camera->setTransform(transform);
    NodePacket packet;
    packet.addPrimitive(camera);

    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, packet);

    REQUIRE(geometry.cameraTransforms.size() == 1);
    REQUIRE(geometry.cameraTransforms[0][3] == glm::vec4(2.f, 0.f, 0.f, 1.f));
}

TEST_CASE("Display geometry refilled from a smaller packet holds only the new contents")
{
    NodePacket twoSquares;
    twoSquares.addPrimitive(buildSquareMesh("/first"));
    twoSquares.addPrimitive(buildSquareMesh("/second"));
    NodePacket oneSquare;
    oneSquare.addPrimitive(buildSquareMesh("/square"));

    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, twoSquares);
    buildDisplayGeometry(geometry, oneSquare);

    REQUIRE(geometry.positions.size() == 4);
    REQUIRE(geometry.topology.triangleIndices.size() == 6);
    REQUIRE(geometry.topology.edgeIndices.size() == 8);
    REQUIRE(geometry.pointPositions.size() == 4);
}

namespace {

std::shared_ptr<geo::Material> makeMaterial(const char* name)
{
    auto material = std::make_shared<geo::Material>();
    material->name = name;
    return material;
}

// Two squares side by side on the ground plane, each made of two triangles (four faces in all),
// whose faces take the given material indices.
std::shared_ptr<geo::Mesh> buildTriangleStrip(
    std::string_view path, const std::vector<intT>& faceMaterials, std::vector<std::shared_ptr<const geo::Material>> table
)
{
    auto mesh = std::make_shared<geo::Mesh>(path);
    const std::vector<Vector3> positions = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 0, 1}, {1, 0, 1}, {2, 0, 1}};
    const std::vector<Offset> points = mesh->addPoints(positions);
    mesh->addFace({points[0], points[1], points[4]});
    mesh->addFace({points[0], points[4], points[3]});
    mesh->addFace({points[1], points[2], points[5]});
    mesh->addFace({points[1], points[5], points[4]});
    auto indices = mesh->addAttribute<intT>(attr::AttributeOwner::FACE, attr::names::materialIndex);
    for (std::size_t face = 0; face < faceMaterials.size(); ++face)
        indices.setValue(face, faceMaterials[face]);
    mesh->setMaterials(std::move(table));
    return mesh;
}

} // namespace

TEST_CASE("Display geometry carries the uv of each vertex, and zero for a mesh without any")
{
    std::shared_ptr<geo::Mesh> mesh = buildSquareMesh("/square");
    NodePacket bare;
    bare.addPrimitive(mesh);
    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, bare);
    REQUIRE(geometry.uvs == std::vector<glm::vec2>(4, glm::vec2(0.f)));

    auto uvs = mesh->addAttribute<Vector3>(attr::AttributeOwner::VERTEX, attr::names::uv);
    uvs.setValue(0, Vector3(0.f, 0.f, 0.f));
    uvs.setValue(1, Vector3(1.f, 0.f, 0.f));
    uvs.setValue(2, Vector3(1.f, 1.f, 0.f));
    uvs.setValue(3, Vector3(0.25f, 0.75f, 0.f));
    NodePacket textured;
    textured.addPrimitive(mesh);
    buildDisplayGeometry(geometry, textured);
    REQUIRE(geometry.uvs.size() == 4);
    REQUIRE(geometry.uvs[1] == glm::vec2(1.f, 0.f));
    REQUIRE(geometry.uvs[2] == glm::vec2(1.f, 1.f));
    REQUIRE(geometry.uvs[3] == glm::vec2(0.25f, 0.75f));
}

TEST_CASE("Display geometry without materials has one batch for every triangle")
{
    NodePacket packet;
    packet.addPrimitive(buildSquareMesh("/first"));
    packet.addPrimitive(buildSquareMesh("/second"));
    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, packet);

    REQUIRE(geometry.materials.empty());
    REQUIRE(geometry.batches == std::vector<MaterialBatch>{{0, 12, -1}});

    // No triangle, no batch.
    NodePacket empty;
    buildDisplayGeometry(geometry, empty);
    REQUIRE(geometry.batches.empty());
}

TEST_CASE("Display geometry groups the triangles of a material and keeps their order")
{
    const auto red = makeMaterial("Red");
    const auto blue = makeMaterial("Blue");
    // The faces alternate between the two materials, and the last one has none.
    NodePacket packet;
    packet.addPrimitive(buildTriangleStrip("/strip", {0, 1, 0, -1}, {red, blue}));
    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, packet);

    REQUIRE(geometry.materials.size() == 2);
    // The face without material comes first, then each material in order of use.
    REQUIRE(geometry.batches == std::vector<MaterialBatch>{{0, 3, -1}, {3, 6, 0}, {9, 3, 1}});
    const std::vector<std::uint32_t>& indices = geometry.topology.triangleIndices;
    REQUIRE(indices.size() == 12);
    // Face 3 first, then faces 0 and 2 (red), then face 1 (blue); each face has three vertices of its own.
    REQUIRE(indices == std::vector<std::uint32_t>{9, 10, 11, 0, 1, 2, 6, 7, 8, 3, 4, 5});
    REQUIRE(geometry.materials[0] == red);
    REQUIRE(geometry.materials[1] == blue);
}

TEST_CASE("Display geometry shares a material between meshes by pointer")
{
    const auto shared = makeMaterial("Shared");
    const auto other = makeMaterial("Other");
    NodePacket packet;
    packet.addPrimitive(buildTriangleStrip("/first", {0, 0, 0, 0}, {shared}));
    packet.addPrimitive(buildTriangleStrip("/second", {1, 1, 0, 0}, {other, shared}));
    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, packet);

    REQUIRE(geometry.materials.size() == 2);
    REQUIRE(geometry.materials[0] == shared);
    REQUIRE(geometry.materials[1] == other);
    // Six triangles of the shared material (four of the first mesh, two of the second) in one batch.
    REQUIRE(geometry.batches == std::vector<MaterialBatch>{{0, 18, 0}, {18, 6, 1}});
}

TEST_CASE("Display geometry ignores a material index outside the table")
{
    const auto red = makeMaterial("Red");
    NodePacket packet;
    packet.addPrimitive(buildTriangleStrip("/strip", {0, 5, -3, 0}, {red}));
    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, packet);
    REQUIRE(geometry.batches == std::vector<MaterialBatch>{{0, 6, -1}, {6, 6, 0}});
}

TEST_CASE("Display geometry of an imported file with two materials has two batches", "[import][material]")
{
    import::clearImportCache();
    const auto file = import::importFile(std::filesystem::path(HELIX_TEST_DATA_DIR) / "import" / "two-materials.glb", {});
    REQUIRE(file->meshes.size() == 1);
    NodePacket packet;
    packet.addPrimitive(std::const_pointer_cast<geo::Mesh>(file->meshes.front()));
    DisplayGeometry geometry;
    buildDisplayGeometry(geometry, packet);

    REQUIRE(geometry.materials.size() == 2);
    REQUIRE(geometry.batches.size() == 2);
    REQUIRE(geometry.batches[0].material == 0);
    REQUIRE(geometry.batches[1].material == 1);
    REQUIRE(geometry.batches[0].firstIndex == 0);
    REQUIRE(geometry.batches[1].firstIndex == geometry.batches[0].indexCount);
    REQUIRE(geometry.batches[0].indexCount + geometry.batches[1].indexCount == geometry.topology.triangleIndices.size());
    REQUIRE(geometry.batches[0].indexCount == geometry.batches[1].indexCount);
}
