#include "Engine/Attribute/AttributeNames.h"
#include "Engine/Core/Types.h"
#include "Engine/Import/BlenderConverter.h"
#include "Engine/Import/ImportFile.h"
#include "Engine/Import/MeshBuilder.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeLoader.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Serializer/Serializer.h"
#include "HelixCore/HelixCore.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>
#include <unistd.h>

// The files read here are in tests/data/import, and tests/data/import/make-test-files.sh writes
// them. They are small and committed.

namespace fs = std::filesystem;
using namespace helix;
using Catch::Approx;

namespace {

fs::path dataFile(const std::string& name)
{
    return fs::path(HELIX_TEST_DATA_DIR) / "import" / name;
}

struct NMReset
{
    NMReset() { nt::nm()._reset(); }
    ~NMReset() { nt::nm()._reset(); }
};

// A folder of its own in the temporary directory, removed with the object.
struct TempFolder
{
    TempFolder()
    {
        static int counter = 0;
        path = fs::temp_directory_path() /
               ("helix_import_tests_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
        fs::create_directories(path);
    }
    ~TempFolder()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

struct Box
{
    Vector3 min = Vector3::Constant(std::numeric_limits<float>::max());
    Vector3 max = Vector3::Constant(std::numeric_limits<float>::lowest());
};

Box boundsOf(const std::vector<std::shared_ptr<const geo::Mesh>>& meshes)
{
    Box box;
    for (const auto& mesh : meshes)
        for (const Vector3& position : mesh->pointPosSpan())
        {
            box.min = box.min.cwiseMin(position);
            box.max = box.max.cwiseMax(position);
        }
    return box;
}

import::ImportOptions defaults() { return {}; }

std::shared_ptr<const import::ImportedFile> read(const std::string& name, import::ImportOptions options = defaults())
{
    import::clearImportCache();
    return import::importFile(dataFile(name), options);
}

// The values of a vertex attribute, or nothing when the mesh has no such attribute.
std::vector<Vector3> vertexVectors(const geo::Mesh& mesh, const char* name)
{
    const auto attribute = mesh.getAttribByName(attr::AttributeOwner::VERTEX, name);
    if (!attribute) return {};
    return attr::AttributeHandleRO<Vector3>(attribute).getAllValues();
}

std::string errorOf(const fs::path& file, const import::ImportOptions& options = defaults())
{
    import::clearImportCache();
    try
    {
        import::importFile(file, options);
    }
    catch (const import::ImportError& error)
    {
        return error.what();
    }
    return {};
}

// Creates a geometry import node pointed at a file and cooks it.
std::shared_ptr<const NodePacket> cookImportNode(const fs::path& file, floatT size = 1.f)
{
    nt::NodeLoader::loadNodes();
    auto& nm = nt::nm();
    const nt::NodeId id = nm.createNode("helix::geometryImport");
    nt::Node& node = nm.getNode(id);
    node.getParameter("filePath").lock()->setString(file.string());
    node.getParameter("size").lock()->setFloat(size);
    nm.cook(id);
    return node.getOutputPacket(0);
}

} // namespace

TEST_CASE("The supported extensions cover the principal 3D formats")
{
    const std::vector<std::string> extensions = import::supportedExtensions();
    for (const char* wanted : {".glb", ".gltf", ".fbx", ".obj", ".stl", ".ply", ".dae", ".3ds", ".3mf",
                               ".dxf", ".x3d", ".x", ".usd", ".usda", ".usdc", ".usdz", ".blend"})
        REQUIRE(std::find(extensions.begin(), extensions.end(), wanted) != extensions.end());
    REQUIRE(import::isSupportedExtension(".GLB"));
    REQUIRE_FALSE(import::isSupportedExtension(".jpg"));
    REQUIRE(core::supportedImportExtensions() == extensions);
}

TEST_CASE("A gltf triangle imports")
{
    const auto file = read("triangle.gltf");
    REQUIRE(file->summary.meshCount == 1);
    REQUIRE(file->summary.pointCount == 3);
    REQUIRE(file->summary.faceCount == 1);
    REQUIRE(file->summary.reader == "Assimp");
    REQUIRE_FALSE(file->summary.hasUvs);

    const Box box = boundsOf(file->meshes);
    REQUIRE(box.max.x() == Approx(1.f));
    REQUIRE(box.max.y() == Approx(1.f));
}

TEST_CASE("A glb cube keeps its normals, its UVs and its material index")
{
    const auto file = read("cube.glb");
    REQUIRE(file->summary.pointCount == 24);
    REQUIRE(file->summary.faceCount == 12);
    REQUIRE(file->summary.hasNormals);
    REQUIRE(file->summary.hasUvs);
    REQUIRE(file->summary.materialCount == 1);
    REQUIRE(file->summary.hasBaseColor);
    REQUIRE(file->summary.baseColor[0] == Approx(0.8f));
    REQUIRE(file->summary.baseColor[2] == Approx(0.1f));

    const geo::Mesh& mesh = *file->meshes.front();
    REQUIRE(mesh.getNumVerts() == 36);

    const std::vector<Vector3> uvs = vertexVectors(mesh, attr::names::uv);
    REQUIRE(uvs.size() == 36);
    for (const Vector3& uv : uvs)
    {
        REQUIRE(uv.x() >= 0.f);
        REQUIRE(uv.x() <= 1.f);
        REQUIRE(uv.y() >= 0.f);
        REQUIRE(uv.y() <= 1.f);
        REQUIRE(uv.z() == 0.f);
    }
    // The corners of the unit square are all there.
    REQUIRE(std::any_of(uvs.begin(), uvs.end(), [](const Vector3& uv) { return uv.x() == 1.f && uv.y() == 1.f; }));

    const std::vector<Vector3> normals = vertexVectors(mesh, attr::names::normal);
    REQUIRE(normals.size() == 36);
    for (const Vector3& normal : normals)
        REQUIRE(normal.norm() == Approx(1.f));

    const auto materials = mesh.getAttribByName(attr::AttributeOwner::FACE, attr::names::materialIndex);
    REQUIRE(materials);
    const attr::AttributeHandleRO<intT> materialIndex(materials);
    for (Offset face = 0; face < mesh.getNumFaces(); ++face)
        REQUIRE(materialIndex.getValue(face) == 0);

    const Box box = boundsOf(file->meshes);
    REQUIRE(box.min.x() == Approx(-1.f));
    REQUIRE(box.max.z() == Approx(1.f));
}

TEST_CASE("A Draco compressed glb gives the same cube")
{
    const auto plain = read("cube.glb");
    const auto draco = read("cube-draco.glb");

    REQUIRE(draco->summary.pointCount == 24);
    REQUIRE(draco->summary.faceCount == 12);
    REQUIRE(draco->summary.hasUvs);
    REQUIRE(draco->summary.hasNormals);

    const Box box = boundsOf(draco->meshes);
    REQUIRE(box.min.x() == Approx(-1.f).margin(1e-2));
    REQUIRE(box.max.y() == Approx(1.f).margin(1e-2));
    REQUIRE(box.max.z() == Approx(1.f).margin(1e-2));
    REQUIRE(boundsOf(plain->meshes).max.x() == Approx(box.max.x()).margin(1e-2));

    const std::vector<Vector3> uvs = vertexVectors(*draco->meshes.front(), attr::names::uv);
    REQUIRE(uvs.size() == 36);
    for (const Vector3& uv : uvs)
    {
        REQUIRE(uv.x() >= -1e-3f);
        REQUIRE(uv.x() <= 1.001f);
    }
}

TEST_CASE("The transforms of a node hierarchy are applied to the points")
{
    SECTION("merged into one mesh")
    {
        const auto file = read("hierarchy.glb");
        REQUIRE(file->meshes.size() == 1);
        REQUIRE(file->summary.pointCount == 48);
        REQUIRE(file->summary.faceCount == 24);

        // The parent cube sits at x 9..11, its child, doubled and moved up, at x 8..12 and y 3..7.
        const Box box = boundsOf(file->meshes);
        REQUIRE(box.min.x() == Approx(8.f));
        REQUIRE(box.max.x() == Approx(12.f));
        REQUIRE(box.min.y() == Approx(-1.f));
        REQUIRE(box.max.y() == Approx(7.f));
        REQUIRE(box.min.z() == Approx(-2.f));
        REQUIRE(box.max.z() == Approx(2.f));
    }
    SECTION("one mesh for each node")
    {
        import::ImportOptions options;
        options.mergeMeshes = false;
        const auto file = read("hierarchy.glb", options);
        REQUIRE(file->meshes.size() == 2);
        REQUIRE(file->meshes[0]->getPath() != file->meshes[1]->getPath());

        const Box parent = boundsOf({file->meshes[0]});
        const Box child = boundsOf({file->meshes[1]});
        REQUIRE(parent.min.x() == Approx(9.f));
        REQUIRE(parent.max.x() == Approx(11.f));
        REQUIRE(child.min.x() == Approx(8.f));
        REQUIRE(child.max.y() == Approx(7.f));
        REQUIRE(child.min.y() == Approx(3.f));
    }
}

TEST_CASE("A stl file stands on its end when the file is Z up")
{
    // The box is 2 x 2 x 4 with its long side along z, from z = 0 to z = 4.
    SECTION("by the convention of the format")
    {
        const auto file = read("box.stl");
        REQUIRE(file->summary.pointCount == 8);
        REQUIRE(file->summary.faceCount == 12);
        const Box box = boundsOf(file->meshes);
        REQUIRE(box.min.y() == Approx(0.f));
        REQUIRE(box.max.y() == Approx(4.f));
        REQUIRE(box.min.z() == Approx(-1.f));
        REQUIRE(box.max.z() == Approx(1.f));
    }
    SECTION("unless the file is said to be Y up")
    {
        import::ImportOptions options;
        options.upAxis = import::UpAxis::Y;
        const Box box = boundsOf(read("box.stl", options)->meshes);
        REQUIRE(box.min.z() == Approx(0.f));
        REQUIRE(box.max.z() == Approx(4.f));
        REQUIRE(box.max.y() == Approx(1.f));
    }
    SECTION("a Y up file said to be Z up is turned")
    {
        import::ImportOptions options;
        options.upAxis = import::UpAxis::Z;
        // The glb cube of side 2 is turned about x, which swaps what it is tall in.
        const auto file = read("hierarchy.glb", options);
        const Box box = boundsOf(file->meshes);
        REQUIRE(box.min.y() == Approx(-2.f));
        REQUIRE(box.max.y() == Approx(2.f));
        REQUIRE(box.min.z() == Approx(-7.f));
        REQUIRE(box.max.z() == Approx(1.f));
    }
}

namespace {

// One triangle, facing +y, in a scene of one instance.
import::RawScene triangleScene()
{
    import::RawScene scene;
    import::RawMesh mesh;
    mesh.name = "tri";
    mesh.positions = {0, 0, 0, 1, 0, 0, 0, 0, 1};
    mesh.normals = {0, 1, 0, 0, 1, 0, 0, 1, 0};
    mesh.faceCounts = {3};
    mesh.indices = {0, 1, 2};
    scene.meshes.push_back(mesh);
    import::RawInstance instance;
    instance.mesh = 0;
    scene.instances.push_back(instance);
    return scene;
}

} // namespace

TEST_CASE("The up axis turns the normals")
{
    const import::RawScene scene = triangleScene();

    import::ImportOptions options;
    options.upAxis = import::UpAxis::Z;
    import::ImportedFile turned = import::buildMeshes(import::RawScene(scene), options, import::UpAxis::Y, "test");
    // A Z up file is stood up by sending z to y and y to -z, so a normal along y ends along -z.
    const std::vector<Vector3> normals = vertexVectors(*turned.meshes.front(), attr::names::normal);
    REQUIRE(normals.size() == 3);
    REQUIRE(normals[0].x() == Approx(0.f).margin(1e-6));
    REQUIRE(normals[0].y() == Approx(0.f).margin(1e-6));
    REQUIRE(normals[0].z() == Approx(-1.f));
    REQUIRE(turned.meshes.front()->getPointPos(2).y() == Approx(1.f));
}

TEST_CASE("A merge of meshes with and without normals computes the missing ones from the faces")
{
    import::RawScene scene = triangleScene();
    // A quad in the plane z = 5 with no normals, wound counter clockwise seen from +z.
    import::RawMesh quad;
    quad.name = "quad";
    quad.positions = {0, 0, 5, 1, 0, 5, 1, 1, 5, 0, 1, 5};
    quad.faceCounts = {4};
    quad.indices = {0, 1, 2, 3};
    scene.meshes.push_back(quad);
    import::RawInstance instance;
    instance.mesh = 1;
    scene.instances.push_back(instance);

    import::ImportOptions options;
    options.mergeMeshes = true;
    const import::ImportedFile merged = import::buildMeshes(std::move(scene), options, import::UpAxis::Y, "test");
    REQUIRE(merged.meshes.size() == 1);
    const std::vector<Vector3> normals = vertexVectors(*merged.meshes.front(), attr::names::normal);
    REQUIRE(normals.size() == 7);
    for (std::size_t corner = 0; corner < 3; ++corner)
        REQUIRE(normals[corner].isApprox(Vector3(0.f, 1.f, 0.f)));
    for (std::size_t corner = 3; corner < 7; ++corner)
    {
        REQUIRE(normals[corner].norm() == Approx(1.f));
        REQUIRE(normals[corner].z() == Approx(1.f));
    }
}

TEST_CASE("A node that mirrors reverses the corners of its faces")
{
    import::RawScene scene = triangleScene();
    // Mirroring x turns the faces inside out, so they are walked backward to face out again.
    scene.instances[0].world(0, 0) = -1.f;
    const import::ImportedFile mirrored = import::buildMeshes(import::RawScene(scene), {}, import::UpAxis::Y, "test");
    const auto corners = mirrored.meshes.front()->getFacePoints(0);
    REQUIRE(corners.size() == 3);
    REQUIRE(corners[0] == 2);
    REQUIRE(corners[2] == 0);
}

TEST_CASE("A ply file keeps its quad and its vertex colors")
{
    const auto file = read("quad.ply");
    REQUIRE(file->summary.pointCount == 4);
    REQUIRE(file->summary.faceCount == 1);
    REQUIRE(file->meshes.front()->getFaceVertCount(0) == 4);
    REQUIRE(file->summary.hasColors);

    const std::vector<Vector3> colors = vertexVectors(*file->meshes.front(), attr::names::color);
    REQUIRE(colors.size() == 4);
    REQUIRE(colors[0].x() == Approx(1.f));
    REQUIRE(colors[0].y() == Approx(0.f));
    REQUIRE(colors[1].y() == Approx(1.f));
}

TEST_CASE("An fbx cube imports")
{
    const auto file = read("cube.fbx");
    REQUIRE(file->summary.meshCount == 1);
    REQUIRE(file->summary.faceCount >= 6);
    REQUIRE(file->summary.pointCount >= 8);
    REQUIRE(file->summary.hasUvs);
    const Box box = boundsOf(file->meshes);
    // The cube is 2 across in whatever unit the file declares, and centred.
    REQUIRE((box.max - box.min).x() == Approx((box.max - box.min).y()).epsilon(1e-3));
    REQUIRE(box.max.x() + box.min.x() == Approx(0.f).margin(1e-3));
}

TEST_CASE("An obj file with texture coordinates keeps them")
{
    const auto file = read("uvsquare.obj");
    REQUIRE(file->summary.pointCount == 4);
    REQUIRE(file->summary.faceCount == 1);
    REQUIRE(file->summary.hasUvs);

    const std::vector<Vector3> uvs = vertexVectors(*file->meshes.front(), attr::names::uv);
    REQUIRE(uvs.size() == 4);
    REQUIRE(uvs[2].x() == Approx(1.f));
    REQUIRE(uvs[2].y() == Approx(1.f));
    REQUIRE(uvs[2].z() == 0.f);
}

TEST_CASE("A usd file reads through Model I/O, as text and as an archive")
{
    for (const char* name : {"quad.usda", "quad.usdz"})
    {
        CAPTURE(name);
        const auto file = read(name);
        REQUIRE(file->summary.reader == "Model I/O");
        // Model I/O hands the quad over as two triangles.
        REQUIRE(file->summary.pointCount == 4);
        REQUIRE(file->summary.faceCount == 2);
        REQUIRE(file->meshes.front()->getFaceVertCount(0) == 3);

        // The quad is 2 x 2 on the ground and its Xform lifts it 3.
        const Box box = boundsOf(file->meshes);
        REQUIRE(box.min.y() == Approx(3.f));
        REQUIRE(box.max.y() == Approx(3.f));
        REQUIRE(box.max.x() == Approx(2.f));
        REQUIRE(box.max.z() == Approx(2.f));

        const std::vector<Vector3> uvs = vertexVectors(*file->meshes.front(), attr::names::uv);
        REQUIRE(uvs.size() == 6);
        REQUIRE(std::any_of(uvs.begin(), uvs.end(), [](const Vector3& uv) { return uv.x() == 1.f; }));
    }
}

TEST_CASE("A blend file is converted by Blender, once")
{
    const fs::path blender = import::findBlender("");
    if (blender.empty()) SKIP("Blender is not installed");

    TempFolder cacheFolder;
    import::setBlendCacheDirectory(cacheFolder.path);

    const std::size_t runsBefore = import::blenderRunCount();
    const auto file = read("cube.blend");
    REQUIRE(file->summary.reader == "Blender + Assimp");
    REQUIRE(file->summary.pointCount == 24);
    REQUIRE(file->summary.faceCount == 12);
    REQUIRE(file->summary.hasUvs);
    REQUIRE(file->summary.hasNormals);
    REQUIRE(import::blenderRunCount() == runsBefore + 1);

    const Box box = boundsOf(file->meshes);
    REQUIRE(box.min.x() == Approx(-1.f).margin(1e-3));
    REQUIRE(box.max.z() == Approx(1.f).margin(1e-3));

    // The memory cache is emptied, so the file is read again, but the glb Blender wrote is reused.
    const std::size_t readsBefore = import::importReadCount();
    const auto again = read("cube.blend");
    REQUIRE(import::importReadCount() == readsBefore + 1);
    REQUIRE(import::blenderRunCount() == runsBefore + 1);
    REQUIRE(again->summary.pointCount == 24);

    // A different Blender setting is a different import, but not a different conversion... which
    // is keyed by the file and the version of Blender alone.
    import::setBlendCacheDirectory({});
}

TEST_CASE("A blend file with no Blender to open it says so")
{
    import::ImportOptions options;
    options.blenderPath = "/nonexistent/Blender";
    const std::string message = errorOf(dataFile("cube.blend"), options);
    REQUIRE(message.find("Blender was not found") != std::string::npos);
}

TEST_CASE("A file that cannot be imported is an error, not a crash")
{
    TempFolder folder;

    SECTION("a file that is not there")
    {
        const std::string message = errorOf(folder.path / "missing.glb");
        REQUIRE(message.find("Couldn't open") != std::string::npos);
        REQUIRE(message.find("missing.glb") != std::string::npos);
    }
    SECTION("an extension Helix does not read lists the ones it does")
    {
        const fs::path file = folder.path / "picture.jpg";
        std::ofstream(file) << "not a model";
        const std::string message = errorOf(file);
        REQUIRE(message.find(".jpg") != std::string::npos);
        REQUIRE(message.find(".glb") != std::string::npos);
        REQUIRE(message.find(".usdz") != std::string::npos);
    }
    SECTION("a folder")
    {
        REQUIRE_FALSE(errorOf(folder.path).empty());
    }
    SECTION("a glb cut short")
    {
        std::ifstream source(dataFile("cube.glb"), std::ios::binary);
        std::stringstream bytes;
        bytes << source.rdbuf();
        const std::string content = bytes.str();
        const fs::path file = folder.path / "truncated.glb";
        std::ofstream(file, std::ios::binary) << content.substr(0, content.size() / 2);

        const std::string message = errorOf(file);
        REQUIRE_FALSE(message.empty());
        REQUIRE(message.size() < 600);
    }
    SECTION("a Draco glb cut short")
    {
        std::ifstream source(dataFile("cube-draco.glb"), std::ios::binary);
        std::stringstream bytes;
        bytes << source.rdbuf();
        const std::string content = bytes.str();
        const fs::path file = folder.path / "truncated-draco.glb";
        std::ofstream(file, std::ios::binary) << content.substr(0, content.size() - content.size() / 4);
        REQUIRE_FALSE(errorOf(file).empty());
    }
    SECTION("bytes that are not a model with a model's name")
    {
        for (const char* name : {"noise.glb", "noise.fbx", "noise.stl", "noise.ply", "noise.usdz", "noise.obj", "noise.dae"})
        {
            CAPTURE(name);
            const fs::path file = folder.path / name;
            std::ofstream(file, std::ios::binary) << "\x01\x02garbage\xff\xfe 12 34 56 \x00\x01";
            const std::string message = errorOf(file);
            // An obj or an stl of garbage may read as an empty model, and then it holds no surface.
            REQUIRE_FALSE(message.empty());
            REQUIRE(message.size() < 600);
        }
    }
    SECTION("an empty file")
    {
        const fs::path file = folder.path / "empty.glb";
        std::ofstream{file};
        REQUIRE_FALSE(errorOf(file).empty());
    }
}

TEST_CASE("A file is read again only when it or the settings change")
{
    TempFolder folder;
    const fs::path file = folder.path / "cube.glb";
    fs::copy_file(dataFile("cube.glb"), file);
    import::clearImportCache();

    const std::size_t reads = import::importReadCount();
    bool fromCache = true;
    const auto first = import::importFile(file, defaults(), &fromCache);
    REQUIRE_FALSE(fromCache);
    REQUIRE(import::importReadCount() == reads + 1);

    const auto second = import::importFile(file, defaults(), &fromCache);
    REQUIRE(fromCache);
    REQUIRE(second == first);
    REQUIRE(import::importReadCount() == reads + 1);

    // Another setting is another import.
    import::ImportOptions split;
    split.mergeMeshes = false;
    import::importFile(file, split, &fromCache);
    REQUIRE_FALSE(fromCache);
    REQUIRE(import::importReadCount() == reads + 2);

    // The file changes: another size and another date.
    fs::copy_file(dataFile("hierarchy.glb"), file, fs::copy_options::overwrite_existing);
    const auto changed = import::importFile(file, defaults(), &fromCache);
    REQUIRE_FALSE(fromCache);
    REQUIRE(changed->summary.pointCount == 48);
    REQUIRE(import::importReadCount() == reads + 3);
}

TEST_CASE_METHOD(NMReset, "The import node reads a glb, scales it and does not read it twice")
{
    import::clearImportCache();
    const std::size_t reads = import::importReadCount();

    auto packet = cookImportNode(dataFile("cube.glb"), 3.f);
    REQUIRE(packet->size() == 1);
    auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(packet->getPrimitive(0));
    REQUIRE(mesh);
    REQUIRE(mesh->getNumPoints() == 24);
    REQUIRE(boundsOf({mesh}).max.x() == Approx(3.f));
    REQUIRE(import::importReadCount() == reads + 1);

    // A second node on the same file takes the kept result, and the first mesh is untouched.
    packet = cookImportNode(dataFile("cube.glb"), 1.f);
    REQUIRE(import::importReadCount() == reads + 1);
    REQUIRE(boundsOf({std::dynamic_pointer_cast<const geo::Mesh>(packet->getPrimitive(0))}).max.x() == Approx(1.f));
    REQUIRE(boundsOf({mesh}).max.x() == Approx(3.f));
    REQUIRE(boundsOf(import::importFile(dataFile("cube.glb"), defaults())->meshes).max.x() == Approx(1.f));
}

TEST_CASE_METHOD(NMReset, "The import node outputs an empty mesh for a file it cannot read")
{
    TempFolder folder;
    const fs::path file = folder.path / "broken.glb";
    std::ofstream(file) << "nothing";

    const auto packet = cookImportNode(file);
    REQUIRE(packet->size() == 1);
    const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(packet->getPrimitive(0));
    REQUIRE(mesh);
    REQUIRE(mesh->getNumPoints() == 0);

    const auto missing = cookImportNode(folder.path / "missing.fbx");
    REQUIRE(std::dynamic_pointer_cast<const geo::Mesh>(missing->getPrimitive(0))->getNumPoints() == 0);
}

TEST_CASE_METHOD(NMReset, "A scene saved before the importer read other formats gives the same obj mesh")
{
    // legacy-obj-import.kut holds a geometryImport node with only the two settings the node had
    // then, a file path and a size of 2. legacy-square.obj is the file it reads.
    TempFolder folder;
    std::ifstream source(dataFile("legacy-obj-import.kut"));
    std::stringstream text;
    text << source.rdbuf();
    std::string scene = text.str();
    const std::string placeholder = "legacy-square.obj";
    const std::size_t at = scene.find(placeholder);
    REQUIRE(at != std::string::npos);
    scene.replace(at, placeholder.size(), dataFile(placeholder).string());
    const fs::path kut = folder.path / "legacy.kut";
    std::ofstream(kut) << scene;

    nt::NodeLoader::loadNodes();
    nt::Serializer().load(nt::nm(), kut.string());

    nt::NodeId importNode = 0;
    for (auto [id, node] : nt::nm().nodes())
        importNode = id;
    REQUIRE(importNode != 0);

    nt::nm().cook(importNode);
    const auto packet = nt::nm().getNode(importNode).getOutputPacket(0);
    REQUIRE(packet->size() == 1);
    const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(packet->getPrimitive(0));
    REQUIRE(mesh);

    // What the obj reader of the first version produced: four points scaled by two, one quad,
    // on the path /mesh, with no normals stored (they are computed when drawn).
    REQUIRE(mesh->getPath() == "/mesh");
    REQUIRE(mesh->getNumPoints() == 4);
    REQUIRE(mesh->getNumFaces() == 1);
    REQUIRE(mesh->getFaceVertCount(0) == 4);
    REQUIRE(mesh->getPointPos(0) == Vector3(-2.f, 0.f, -2.f));
    REQUIRE(mesh->getPointPos(2) == Vector3(2.f, 0.f, 2.f));
    REQUIRE_FALSE(mesh->getAttribByName(attr::AttributeOwner::VERTEX, attr::names::normal));
    // The node settings added since are at their defaults.
    nt::Node& node = nt::nm().getNode(importNode);
    REQUIRE(node.getParameter("mergeMeshes").lock()->evalInt() == 1);
    REQUIRE(node.getParameter("upAxis").lock()->evalString() == "auto");
}

TEST_CASE("The facade imports a file into a node, without touching the file")
{
    REQUIRE(core::initialize({}).ok);
    REQUIRE(core::newScene().ok);

    TempFolder folder;
    const fs::path file = folder.path / "my cube.glb";
    fs::copy_file(dataFile("cube.glb"), file);
    const auto before = fs::last_write_time(file);
    const auto sizeBefore = fs::file_size(file);

    const core::CreateNodeResult result = core::importFile(file.string());
    REQUIRE(result.status.ok);
    REQUIRE(result.id != 0);
    REQUIRE(core::getString(result.id, "filePath", 0).value == file.string());
    REQUIRE(core::listNodes().size() == 1);
    REQUIRE(core::listNodes().front().type == "helix::geometryImport");
    REQUIRE(core::listNodes().front().name == "my_cube");
    REQUIRE_FALSE(core::lastImportSummary().empty());
    REQUIRE(core::lastImportSummary().find("24 points") != std::string::npos);
    REQUIRE(core::lastImportSummary().find("UVs") != std::string::npos);

    REQUIRE(core::setDisplayNode(result.id).ok);
    const core::GeometrySummary summary = core::cookDisplaySummary();
    REQUIRE(summary.status.ok);
    REQUIRE(summary.pointCount == 24);
    REQUIRE(summary.triangleCount == 12);

    REQUIRE((fs::last_write_time(file) == before));
    REQUIRE(fs::file_size(file) == sizeBefore);

    SECTION("a second import of the same file is a second node with a free name")
    {
        const core::CreateNodeResult again = core::importFile(file.string());
        REQUIRE(again.status.ok);
        REQUIRE(again.id != result.id);
        REQUIRE(core::listNodes().size() == 2);
        REQUIRE(core::listNodes()[0].name != core::listNodes()[1].name);
    }
    SECTION("a file that cannot be imported creates no node")
    {
        const core::CreateNodeResult failed = core::importFile((folder.path / "nothing.fbx").string());
        REQUIRE_FALSE(failed.status.ok);
        REQUIRE(failed.id == 0);
        REQUIRE(failed.status.error.find("nothing.fbx") != std::string::npos);
        REQUIRE(core::listNodes().size() == 1);

        const fs::path unknown = folder.path / "model.abc";
        std::ofstream(unknown) << "x";
        const core::CreateNodeResult unsupported = core::importFile(unknown.string());
        REQUIRE_FALSE(unsupported.status.ok);
        REQUIRE(unsupported.status.error.find(".glb") != std::string::npos);
    }
    SECTION("the cache folder is created")
    {
        REQUIRE(core::setCacheDirectory((folder.path / "cache" / "blend").string()).ok);
        REQUIRE(fs::is_directory(folder.path / "cache" / "blend"));
        REQUIRE_FALSE(core::setCacheDirectory("").ok);
        import::setBlendCacheDirectory({});
    }
}

namespace {

// The buffer of a triangle whose first corner is at (x, 0, 0).
void writeTriangleBuffer(const fs::path& folder, float x)
{
    const float positions[9] = {x, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    std::ofstream bin(folder / "tri.bin", std::ios::binary);
    bin.write(reinterpret_cast<const char*>(positions), sizeof(positions));
}

// A gltf of one triangle whose buffer is the separate file tri.bin.
void writeSplitGltf(const fs::path& folder)
{
    std::ofstream json(folder / "tri.gltf");
    json << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
            R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
            R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",)"
            R"("min":[-9,0,0],"max":[9,1,0]}],)"
            R"("bufferViews":[{"buffer":0,"byteLength":36}],"buffers":[{"uri":"tri.bin","byteLength":36}]})";
}

} // namespace

TEST_CASE("The import cache sees a gltf buffer that changed behind an unchanged gltf")
{
    TempFolder folder;
    writeTriangleBuffer(folder.path, 0.f);
    writeSplitGltf(folder.path);
    const fs::path gltf = folder.path / "tri.gltf";
    const fs::path bin = folder.path / "tri.bin";
    import::clearImportCache();

    bool fromCache = true;
    const auto first = import::importFile(gltf, defaults(), &fromCache);
    REQUIRE_FALSE(fromCache);
    REQUIRE(first->meshes.front()->getPointPos(0).x() == Approx(0.f));

    // Nothing changed: the second import is the first one, without a read.
    const std::size_t reads = import::importReadCount();
    import::importFile(gltf, defaults(), &fromCache);
    REQUIRE(fromCache);
    REQUIRE(import::importReadCount() == reads);

    // Only the buffer is rewritten, and its date is moved on by hand so the clock does not matter.
    const auto before = fs::last_write_time(bin);
    writeTriangleBuffer(folder.path, 5.f);
    fs::last_write_time(bin, before + std::chrono::seconds(10));
    const auto second = import::importFile(gltf, defaults(), &fromCache);
    REQUIRE_FALSE(fromCache);
    REQUIRE(second->meshes.front()->getPointPos(0).x() == Approx(5.f));
    REQUIRE(import::importReadCount() == reads + 1);
}
