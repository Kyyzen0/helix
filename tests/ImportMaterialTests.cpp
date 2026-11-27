#include "Engine/Attribute/AttributeHandle.h"
#include "Engine/Attribute/AttributeNames.h"
#include "Engine/Core/Types.h"
#include "Engine/GeometryAlgorithms/BooleanUtils.h"
#include "Engine/GeometryAlgorithms/MeshShapes.h"
#include "Engine/Import/ImageDecode.h"
#include "Engine/Import/ImportFile.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeLoader.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Primitives/Mesh.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unistd.h>

// What the import keeps of materials: the base colour factor and the base colour picture, from the
// files tests/data/import/make_test_files.py writes.

namespace fs = std::filesystem;
using namespace helix;
using Catch::Approx;

namespace {

fs::path dataFile(const std::string& name) { return fs::path(HELIX_TEST_DATA_DIR) / "import" / name; }

struct NMReset
{
    NMReset() { nt::nm()._reset(); }
    ~NMReset() { nt::nm()._reset(); }
};

struct TempFolder
{
    TempFolder()
    {
        static int counter = 0;
        path = fs::temp_directory_path() /
               ("helix_material_tests_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
        fs::create_directories(path);
    }
    ~TempFolder()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

std::shared_ptr<const import::ImportedFile> read(const fs::path& file, import::ImportOptions options = {}, bool* fromCache = nullptr)
{
    import::clearImportCache();
    return import::importFile(file, options, fromCache);
}

using Pixel = std::array<std::uint8_t, 4>;
constexpr Pixel red{255, 0, 0, 255};
constexpr Pixel green{0, 255, 0, 255};
constexpr Pixel blue{0, 0, 255, 255};
constexpr Pixel white{255, 255, 255, 255};

// The texel the uv of the point at (x, y) designates, which has to be the quadrant that sits at
// that corner of the quad: the picture is drawn upright on it.
Pixel texelAtCorner(const geo::Mesh& mesh, float x, float y)
{
    const geo::Material& material = *mesh.materials().front();
    const auto uvs = attr::AttributeHandleRO<Vector3>(mesh.getAttribByName(attr::AttributeOwner::VERTEX, attr::names::uv));
    for (Offset vertex = 0; vertex < mesh.getNumVerts(); ++vertex)
    {
        const Vector3 position = mesh.getPosFromVert(vertex);
        if (position.x() == x && position.y() == y)
        {
            const Vector3 uv = uvs.getValue(vertex);
            return material.baseColorTexture->sampleNearest(uv.x(), uv.y());
        }
    }
    FAIL("no point at the corner");
    return {};
}

void requireQuadrants(const geo::Mesh& mesh)
{
    REQUIRE(mesh.materials().size() == 1);
    const geo::Material& material = *mesh.materials().front();
    REQUIRE(material.baseColorTexture);
    const geo::Image& image = *material.baseColorTexture;
    REQUIRE(image.width == 4);
    REQUIRE(image.height == 4);
    REQUIRE(image.byteSize() == 4 * 4 * 4);
    // Row 0 is the top of the picture.
    REQUIRE(image.pixel(0, 0) == red);
    REQUIRE(image.pixel(1, 1) == red);
    REQUIRE(image.pixel(3, 0) == green);
    REQUIRE(image.pixel(2, 1) == green);
    REQUIRE(image.pixel(0, 3) == blue);
    REQUIRE(image.pixel(1, 2) == blue);
    REQUIRE(image.pixel(3, 3) == white);
    REQUIRE(image.pixel(2, 2) == white);

    // The quad is upright: red at its top left, green at its top right, blue at its bottom
    // left and white at its bottom right.
    REQUIRE(texelAtCorner(mesh, -1.f, 1.f) == red);
    REQUIRE(texelAtCorner(mesh, 1.f, 1.f) == green);
    REQUIRE(texelAtCorner(mesh, -1.f, -1.f) == blue);
    REQUIRE(texelAtCorner(mesh, 1.f, -1.f) == white);
}

// A copy of the external textured quad in a folder of its own.
void copyExternalQuad(const fs::path& folder, bool withPicture = true)
{
    fs::copy_file(dataFile("textured-quad.gltf"), folder / "textured-quad.gltf");
    fs::copy_file(dataFile("textured-quad.bin"), folder / "textured-quad.bin");
    if (withPicture) fs::copy_file(dataFile("quadrants.png"), folder / "quadrants.png");
}

} // namespace

TEST_CASE("A glb with an embedded picture decodes it and keeps the base colour factor", "[import][material]")
{
    const auto file = read(dataFile("textured-quad.glb"));
    const geo::Mesh& mesh = *file->meshes.front();
    requireQuadrants(mesh);

    const geo::Material& material = *mesh.materials().front();
    REQUIRE(material.name == "Painted");
    REQUIRE(material.baseColor.x() == Approx(0.9f));
    REQUIRE(material.baseColor.y() == Approx(0.8f));
    REQUIRE(material.baseColor.z() == Approx(0.7f));
    REQUIRE(material.baseColor.w() == Approx(1.f));
    REQUIRE(material.missingTexture.empty());
    REQUIRE(material.baseColorTexture->source.find("textured-quad.glb#embedded:") != std::string::npos);

    REQUIRE(file->summary.texturedMaterialCount == 1);
    REQUIRE(file->summary.decodedTextureCount == 1);
    REQUIRE(file->summary.textureBytes == 64);
    REQUIRE(file->summary.missingTextures.empty());
    REQUIRE(file->summary.text().find("1 decoded") != std::string::npos);
}

TEST_CASE("A gltf with an external picture decodes it, and the cache watches the file", "[import][material]")
{
    TempFolder folder;
    copyExternalQuad(folder.path);
    const fs::path gltf = folder.path / "textured-quad.gltf";

    bool fromCache = true;
    const auto first = read(gltf, {}, &fromCache);
    REQUIRE_FALSE(fromCache);
    requireQuadrants(*first->meshes.front());
    REQUIRE(first->meshes.front()->materials().front()->baseColorTexture->source ==
            (folder.path / "quadrants.png").string());

    // Nothing changed: no new read.
    const std::size_t reads = import::importReadCount();
    import::importFile(gltf, {}, &fromCache);
    REQUIRE(fromCache);
    REQUIRE(import::importReadCount() == reads);

    // Only the picture is replaced (all red, with its date moved on by hand): the file is read again.
    const fs::path png = folder.path / "quadrants.png";
    const auto before = fs::last_write_time(png);
    fs::copy_file(dataFile("wide-9000x2.png"), png, fs::copy_options::overwrite_existing);
    fs::last_write_time(png, before + std::chrono::seconds(10));
    const auto second = import::importFile(gltf, {}, &fromCache);
    REQUIRE_FALSE(fromCache);
    REQUIRE(import::importReadCount() == reads + 1);
    const geo::Image& replaced = *second->meshes.front()->materials().front()->baseColorTexture;
    REQUIRE(replaced.width == 8192);
    REQUIRE(replaced.pixel(0, 0) == Pixel{200, 100, 50, 255});
}

TEST_CASE("A usd material reads its base colour texture through Model I/O", "[import][material]")
{
    const auto file = read(dataFile("textured-quad.usda"));
    REQUIRE(file->summary.reader == "Model I/O");
    REQUIRE(file->summary.decodedTextureCount == 1);
    requireQuadrants(*file->meshes.front());

    // A picture the usd names and that is not there is a note, not a failure.
    TempFolder folder;
    fs::copy_file(dataFile("textured-quad.usda"), folder.path / "textured-quad.usda");
    const auto missing = read(folder.path / "textured-quad.usda");
    const geo::Material& material = *missing->meshes.front()->materials().front();
    REQUIRE_FALSE(material.baseColorTexture);
    REQUIRE_FALSE(material.missingTexture.empty());
    REQUIRE(missing->summary.missingTextures.size() == 1);
}

TEST_CASE("A picture over the largest texture side is scaled down and says so", "[import][material]")
{
    const import::DecodeResult result = import::decodeImageFile(dataFile("wide-9000x2.png"));
    REQUIRE(result.image);
    REQUIRE(result.image->width == import::kMaxTextureSide);
    REQUIRE(result.image->height >= 1);
    REQUIRE(result.image->height <= 2);
    REQUIRE(result.image->source.find("scaled down") != std::string::npos);
    REQUIRE(result.image->byteSize() == std::size_t(result.image->width) * result.image->height * 4);

    // A bad file is an error with a reason, never a crash.
    const import::DecodeResult bad = import::decodeImage("not a picture", 13, "x");
    REQUIRE_FALSE(bad.image);
    REQUIRE_FALSE(bad.error.empty());
    REQUIRE_FALSE(import::decodeImageFile(dataFile("nothing.png")).image);
}

TEST_CASE("A missing picture leaves the material with its factor and does not fail the import", "[import][material]")
{
    TempFolder folder;
    copyExternalQuad(folder.path, false);
    const auto file = read(folder.path / "textured-quad.gltf");

    const geo::Mesh& mesh = *file->meshes.front();
    REQUIRE(mesh.getNumFaces() == 2);
    REQUIRE(mesh.materials().size() == 1);
    const geo::Material& material = *mesh.materials().front();
    REQUIRE_FALSE(material.baseColorTexture);
    REQUIRE(material.baseColor.x() == Approx(0.9f));
    REQUIRE(material.missingTexture.find("quadrants.png") != std::string::npos);

    REQUIRE(file->summary.texturedMaterialCount == 1);
    REQUIRE(file->summary.decodedTextureCount == 0);
    REQUIRE(file->summary.missingTextures.size() == 1);
    REQUIRE(file->summary.text().find("Missing") != std::string::npos);

    SECTION("a picture that is not a picture says so")
    {
        std::ofstream(folder.path / "quadrants.png") << "this is text";
        const auto damaged = read(folder.path / "textured-quad.gltf");
        const geo::Material& flawed = *damaged->meshes.front()->materials().front();
        REQUIRE_FALSE(flawed.baseColorTexture);
        REQUIRE(flawed.missingTexture.find("could not be loaded") != std::string::npos);
    }
}

TEST_CASE("The material index of a face points into the table of its mesh", "[import][material]")
{
    const auto file = read(dataFile("textured-quad.glb"));
    const geo::Mesh& mesh = *file->meshes.front();
    const attr::AttributeHandleRO<intT> indices(mesh.getAttribByName(attr::AttributeOwner::FACE, attr::names::materialIndex));
    for (Offset face = 0; face < mesh.getNumFaces(); ++face)
    {
        REQUIRE(indices.getValue(face) == 0);
        REQUIRE(mesh.materialOfFace(face) == mesh.materials()[0]);
    }
    REQUIRE(mesh.materialOfFace(99) == nullptr);

    // A mesh with no table has no material, whatever its indices say.
    REQUIRE(geo::Mesh().materialOfFace(0) == nullptr);
    // A file with no material gets the default one of the reader, which has no picture.
    const auto bare = read(dataFile("triangle.gltf"));
    for (const auto& material : bare->meshes.front()->materials())
        REQUIRE_FALSE(material->baseColorTexture);
}

TEST_CASE("Meshes with different materials keep their own table, merged or split", "[import][material]")
{
    const auto merged = read(dataFile("two-materials.glb"));
    REQUIRE(merged->meshes.size() == 1);
    const geo::Mesh& both = *merged->meshes.front();
    REQUIRE(both.materials().size() == 2);
    REQUIRE(both.materials()[0]->name == "Red");
    REQUIRE(both.materials()[1]->name == "Blue");
    const attr::AttributeHandleRO<intT> indices(both.getAttribByName(attr::AttributeOwner::FACE, attr::names::materialIndex));
    REQUIRE(both.getNumFaces() == 4);
    REQUIRE(indices.getValue(0) == 0);
    REQUIRE(indices.getValue(1) == 0);
    REQUIRE(indices.getValue(2) == 1);
    REQUIRE(indices.getValue(3) == 1);
    REQUIRE(both.materialOfFace(3)->baseColor.z() == Approx(1.f));

    import::ImportOptions split;
    split.mergeMeshes = false;
    const auto apart = read(dataFile("two-materials.glb"), split);
    REQUIRE(apart->meshes.size() == 2);
    // Each mesh keeps only the material it uses, and its faces are renumbered to match.
    for (const auto& mesh : apart->meshes)
    {
        REQUIRE(mesh->materials().size() == 1);
        const attr::AttributeHandleRO<intT> own(mesh->getAttribByName(attr::AttributeOwner::FACE, attr::names::materialIndex));
        REQUIRE(own.getValue(0) == 0);
    }
    REQUIRE(apart->meshes[0]->materials()[0]->name == "Red");
    REQUIRE(apart->meshes[1]->materials()[0]->name == "Blue");
    // The materials are the same objects as in the merged read of the same file only within one
    // read: here the two meshes do not share any.
    REQUIRE(apart->meshes[0]->materials()[0] != apart->meshes[1]->materials()[0]);
}

TEST_CASE("Merging meshes brings the materials of the second into the table of the first", "[import][material]")
{
    auto shared = std::make_shared<geo::Material>();
    shared->name = "Shared";
    auto first = std::make_shared<geo::Material>();
    first->name = "First";
    auto second = std::make_shared<geo::Material>();
    second->name = "Second";

    auto makeMesh = [](const Vector3& center, std::vector<std::shared_ptr<const geo::Material>> table, std::vector<intT> faces) {
        auto mesh = utils::buildCube({1, 1, 1}, center);
        auto indices = mesh->addAttribute<intT>(attr::AttributeOwner::FACE, attr::names::materialIndex);
        for (Offset face = 0; face < mesh->getNumFaces(); ++face)
            indices.setValue(face, faces[face % faces.size()]);
        mesh->setMaterials(std::move(table));
        return mesh;
    };

    SECTION("indices are offset and a shared material is not added twice")
    {
        auto a = makeMesh({0, 0, 0}, {first, shared}, {0, 1});
        auto b = makeMesh({5, 0, 0}, {second, shared}, {0, 1, 7, -1});
        const Offset aFaces = a->getNumFaces();
        a->merge(*b);

        REQUIRE(a->materials().size() == 3);
        REQUIRE(a->materials()[2] == second);
        const attr::AttributeHandleRO<intT> indices(a->getAttribByName(attr::AttributeOwner::FACE, attr::names::materialIndex));
        REQUIRE(indices.getValue(0) == 0);
        REQUIRE(indices.getValue(1) == 1);
        // b's face 0 was its material 0 (second), face 1 its material 1 (shared, already at 1).
        REQUIRE(indices.getValue(aFaces + 0) == 2);
        REQUIRE(indices.getValue(aFaces + 1) == 1);
        // An index outside b's table, and no material, both end as no material.
        REQUIRE(indices.getValue(aFaces + 2) == -1);
        REQUIRE(indices.getValue(aFaces + 3) == -1);
    }

    SECTION("a mesh with materials merged into one with none, and the other way round")
    {
        auto plain = utils::buildCube({1, 1, 1}, {0, 0, 0});
        auto painted = makeMesh({5, 0, 0}, {first}, {0});
        const Offset plainFaces = plain->getNumFaces();
        plain->merge(*painted);
        REQUIRE(plain->materials().size() == 1);
        const attr::AttributeHandleRO<intT> indices(plain->getAttribByName(attr::AttributeOwner::FACE, attr::names::materialIndex));
        REQUIRE(indices.getValue(0) == -1);
        REQUIRE(indices.getValue(plainFaces) == 0);

        auto base = makeMesh({0, 0, 0}, {first}, {0});
        auto none = utils::buildCube({1, 1, 1}, {5, 0, 0});
        const Offset baseFaces = base->getNumFaces();
        base->merge(*none);
        const attr::AttributeHandleRO<intT> after(base->getAttribByName(attr::AttributeOwner::FACE, attr::names::materialIndex));
        REQUIRE(after.getValue(0) == 0);
        REQUIRE(after.getValue(baseFaces) == -1);
    }

    SECTION("meshes with no material at all are left as they were")
    {
        auto a = utils::buildCube({1, 1, 1}, {0, 0, 0});
        auto b = utils::buildCube({1, 1, 1}, {5, 0, 0});
        a->merge(*b);
        REQUIRE(a->materials().empty());
        REQUIRE_FALSE(a->getAttribByName(attr::AttributeOwner::FACE, attr::names::materialIndex));
    }

    SECTION("a copy shares the materials by pointer")
    {
        auto a = makeMesh({0, 0, 0}, {first}, {0});
        geo::Mesh copy(*a);
        REQUIRE(copy.materials().size() == 1);
        REQUIRE(copy.materials()[0] == first);
        geo::Mesh assigned;
        assigned = *a;
        REQUIRE(assigned.materials()[0] == first);
    }
}

TEST_CASE("A boolean of two meshes carries the materials of both", "[import][material]")
{
    auto first = std::make_shared<geo::Material>();
    first->name = "First";
    auto second = std::make_shared<geo::Material>();
    second->name = "Second";
    auto paint = [](const std::shared_ptr<geo::Mesh>& mesh, std::shared_ptr<const geo::Material> material) {
        auto indices = mesh->addAttribute<intT>(attr::AttributeOwner::FACE, attr::names::materialIndex);
        for (Offset face = 0; face < mesh->getNumFaces(); ++face)
            indices.setValue(face, 0);
        mesh->setMaterials({std::move(material)});
    };
    auto a = utils::buildCube({2, 2, 2}, {0, 0, 0});
    auto b = utils::buildCube({2, 2, 2}, {1, 1, 1});
    paint(a, first);
    paint(b, second);

    const auto result = utils::booleanMesh(*a, *b, utils::BooleanOp::UNION);
    REQUIRE(result);
    REQUIRE(result->materials().size() == 2);
    bool usesFirst = false;
    bool usesSecond = false;
    for (Offset face = 0; face < result->getNumFaces(); ++face)
    {
        const auto material = result->materialOfFace(face);
        REQUIRE(material);
        usesFirst = usesFirst || material == first;
        usesSecond = usesSecond || material == second;
    }
    REQUIRE(usesFirst);
    REQUIRE(usesSecond);
}

TEST_CASE_METHOD(NMReset, "A node downstream of the import keeps the materials", "[import][material]")
{
    nt::NodeLoader::loadNodes();
    auto& nm = nt::nm();
    const nt::NodeId importer = nm.createNode("helix::geometryImport");
    nm.getNode(importer).getParameter("filePath").lock()->setString(dataFile("textured-quad.glb").string());
    const nt::NodeId transform = nm.createNode("helix::transform");
    nm.connectNodes(importer, 0, transform, 0);
    nm.getNode(transform).getParameter("translate").lock()->setFloat(4.f, 0);
    nm.cook(transform);

    const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(nm.getNode(transform).getOutputPacket(0)->getPrimitive(0));
    REQUIRE(mesh);
    REQUIRE(mesh->getPointPos(0).x() == Approx(3.f));
    REQUIRE(mesh->materials().size() == 1);
    REQUIRE(mesh->materials()[0]->baseColorTexture->pixel(0, 0) == red);
    REQUIRE(mesh->materialOfFace(1) == mesh->materials()[0]);

    // The import node's own output shares the materials with its source rather than copying pixels.
    const auto upstream = std::dynamic_pointer_cast<const geo::Mesh>(nm.getNode(importer).getOutputPacket(0)->getPrimitive(0));
    REQUIRE(upstream->materials().front() == mesh->materials().front());
    REQUIRE(upstream->materials().front()->baseColorTexture == mesh->materials().front()->baseColorTexture);
}

TEST_CASE_METHOD(NMReset, "Merging two imports of a textured file keeps the uv of the second copy", "[import][material][merge]")
{
    nt::NodeLoader::loadNodes();
    auto& nm = nt::nm();
    const nt::NodeId first = nm.createNode("helix::geometryImport");
    const nt::NodeId second = nm.createNode("helix::geometryImport");
    for (const nt::NodeId importer : {first, second})
        nm.getNode(importer).getParameter("filePath").lock()->setString(dataFile("textured-quad.glb").string());
    const nt::NodeId merge = nm.createNode("helix::merge");
    nm.connectNodes(first, 0, merge, 0);
    nm.connectNodes(second, 0, merge, 1);
    nm.cook(merge);

    const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(nm.getNode(merge).getOutputPacket(0)->getPrimitive(0));
    REQUIRE(mesh);
    REQUIRE(mesh->getNumFaces() == 4);
    const Offset half = mesh->getNumVerts() / 2;
    REQUIRE(half * 2 == mesh->getNumVerts());
    const auto attribute = mesh->getAttribByName(attr::AttributeOwner::VERTEX, attr::names::uv);
    REQUIRE(attribute);
    REQUIRE(attribute->getSize() == mesh->getNumVerts());
    const attr::AttributeHandleRO<Vector3> uvs(attribute);
    bool anyNonZero = false;
    for (Offset vertex = 0; vertex < half; ++vertex)
    {
        REQUIRE(uvs.getValue(half + vertex) == uvs.getValue(vertex));
        anyNonZero = anyNonZero || uvs.getValue(vertex) != Vector3(0, 0, 0);
    }
    REQUIRE(anyNonZero);
    // The two copies share the material, so the picture is sampled over both.
    REQUIRE(mesh->materials().size() == 1);
}

