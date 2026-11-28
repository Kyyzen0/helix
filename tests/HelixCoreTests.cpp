#include "Engine/Import/ImportFile.h"
#include "Engine/Network/NetworkManager.h"
#include "HelixCore/HelixCore.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <utility>
#include <vector>
#include <string>
#include <thread>
#include <unistd.h>

namespace core = helix::core;
namespace fs = std::filesystem;

namespace {

// Starts the engine once and gives every test an empty scene.
struct CoreFixture
{
    CoreFixture()
    {
        REQUIRE(core::initialize({}).ok);
        REQUIRE(core::newScene().ok);
    }
};

// A folder of its own under the temporary directory, removed when the test is done.
struct TempFolder
{
    TempFolder()
    {
        static int counter = 0;
        path = fs::temp_directory_path() /
               ("helix_core_tests_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
        fs::create_directories(path);
    }
    ~TempFolder()
    {
        std::error_code ignored;
        fs::permissions(path, fs::perms::owner_all, fs::perm_options::add, ignored);
        fs::remove_all(path, ignored);
    }
    fs::path file(const std::string& name) const { return path / name; }
    fs::path path;
};

std::string readFile(const fs::path& path)
{
    std::ifstream stream(path);
    std::stringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

core::NodeId createDisplayedCube()
{
    const core::CreateNodeResult cube = core::createNode("helix::cube", "", 0.f, 0.f);
    REQUIRE(cube.status.ok);
    REQUIRE(core::setDisplayNode(cube.id).ok);
    return cube.id;
}

// The largest distance from the origin along x over the positions.
float widestX(const std::vector<float>& positions)
{
    float widest = 0.f;
    for (std::size_t index = 0; index + 2 < positions.size(); index += 3)
        widest = std::max(widest, std::abs(positions[index]));
    return widest;
}

} // namespace

TEST_CASE_METHOD(CoreFixture, "The facade starts and reports a version")
{
    REQUIRE(core::isInitialized());
    REQUIRE_FALSE(core::version().empty());

    const std::vector<std::string> types = core::listNodeTypes();
    REQUIRE(std::find(types.begin(), types.end(), "helix::cube") != types.end());

    // Initializing a second time does nothing and succeeds.
    REQUIRE(core::initialize({}).ok);
}

TEST_CASE_METHOD(CoreFixture, "A new scene is empty and unmodified")
{
    REQUIRE(core::listNodes().empty());
    REQUIRE(core::listLinks().empty());
    REQUIRE(core::displayNode() == 0);
    REQUIRE_FALSE(core::isSceneModified());
}

TEST_CASE_METHOD(CoreFixture, "The display cube cooks into eight points and twelve triangles")
{
    const core::NodeId cube = createDisplayedCube();
    REQUIRE(core::displayNode() == cube);

    const core::GeometrySummary summary = core::cookDisplaySummary();
    REQUIRE(summary.status.ok);
    REQUIRE(summary.primitiveCount == 1);
    REQUIRE(summary.pointCount == 8);
    REQUIRE(summary.faceCount == 6);
    REQUIRE(summary.triangleCount == 12);

    const core::DisplayBuffers buffers = core::cookDisplayBuffers();
    REQUIRE(buffers.status.ok);
    REQUIRE(buffers.positions.size() % 3 == 0);
    REQUIRE(buffers.normals.size() == buffers.positions.size());
    REQUIRE(buffers.triangleIndices.size() == 36);
    REQUIRE(buffers.pointPositions.size() == 8 * 3);

    const std::size_t vertexCount = buffers.positions.size() / 3;
    for (const std::uint32_t index : buffers.triangleIndices)
        REQUIRE(index < vertexCount);

    // The default cube is one unit wide and centered on the origin.
    REQUIRE(widestX(buffers.positions) == 0.5f);
}

TEST_CASE_METHOD(CoreFixture, "Changing the size of the cube moves its positions")
{
    const core::NodeId cube = createDisplayedCube();
    const float before = widestX(core::cookDisplayBuffers().positions);

    REQUIRE(core::setVector3(cube, "size", 4.f, 4.f, 4.f).ok);

    const core::Vector3Value size = core::getVector3(cube, "size");
    REQUIRE(size.status.ok);
    REQUIRE(size.x == 4.f);

    const core::DisplayBuffers after = core::cookDisplayBuffers();
    REQUIRE(after.status.ok);
    REQUIRE(widestX(after.positions) == before * 4.f);
    REQUIRE(core::cookDisplaySummary().pointCount == 8);
}

TEST_CASE_METHOD(CoreFixture, "Parameters are listed and read by type")
{
    const core::NodeId cube = createDisplayedCube();

    const std::vector<core::ParameterInfo> parameters = core::listParameters(cube);
    const auto size = std::find_if(parameters.begin(), parameters.end(), [](const auto& parameter) {
        return parameter.name == "size";
    });
    REQUIRE(size != parameters.end());
    REQUIRE(size->type == core::ParameterType::Float);
    REQUIRE(size->componentCount == 3);

    REQUIRE(core::getFloat(cube, "size", 1).value == 1.f);
    REQUIRE(core::setFloat(cube, "size", 1, 3.f).ok);
    REQUIRE(core::getFloat(cube, "size", 1).value == 3.f);

    // The wrong type, an unknown name and a component past the last are refused.
    REQUIRE_FALSE(core::getInt(cube, "size", 0).status.ok);
    REQUIRE_FALSE(core::setString(cube, "size", 0, "x").ok);
    REQUIRE_FALSE(core::getFloat(cube, "nope", 0).status.ok);
    REQUIRE_FALSE(core::setFloat(cube, "size", 3, 1.f).ok);
    REQUIRE_FALSE(core::getFloat(9999, "size", 0).status.ok);
    REQUIRE(core::listParameters(9999).empty());
}

TEST_CASE_METHOD(CoreFixture, "A network is edited through creating, moving and linking")
{
    const core::CreateNodeResult first = core::createNode("helix::cube", "", 10.f, 20.f);
    const core::CreateNodeResult second = core::createNode("helix::transform", "mover", 0.f, 0.f);
    REQUIRE(first.status.ok);
    REQUIRE(second.status.ok);

    REQUIRE(core::moveNode(second.id, 5.f, 6.f).ok);
    REQUIRE(core::connectNodes(first.id, 0, second.id, 0).ok);

    const std::vector<core::NodeInfo> nodes = core::listNodes();
    REQUIRE(nodes.size() == 2);
    REQUIRE(nodes[0].name == "cube1");
    REQUIRE(nodes[0].type == "helix::cube");
    REQUIRE(nodes[0].x == 10.f);
    REQUIRE(nodes[0].y == 20.f);
    REQUIRE(nodes[1].name == "mover");
    REQUIRE(nodes[1].x == 5.f);

    const std::vector<core::LinkInfo> links = core::listLinks();
    REQUIRE(links.size() == 1);
    REQUIRE(links[0].sourceNode == first.id);
    REQUIRE(links[0].targetNode == second.id);

    // The geometry of a displayed transform comes through the link.
    REQUIRE(core::setDisplayNode(second.id).ok);
    REQUIRE(core::cookDisplaySummary().pointCount == 8);

    REQUIRE(core::disconnectNodes(first.id, 0, second.id, 0).ok);
    REQUIRE(core::listLinks().empty());
    REQUIRE_FALSE(core::disconnectNodes(first.id, 0, second.id, 0).ok);

    REQUIRE(core::deleteNode(second.id).ok);
    REQUIRE(core::listNodes().size() == 1);
    REQUIRE(core::displayNode() == 0);
}

TEST_CASE_METHOD(CoreFixture, "A name that is taken or cannot name a node is refused, not changed")
{
    REQUIRE(core::createNode("helix::cube", "box", 0.f, 0.f).status.ok);

    REQUIRE_FALSE(core::createNode("helix::cube", "box", 0.f, 0.f).status.ok);
    REQUIRE_FALSE(core::createNode("helix::cube", "has space", 0.f, 0.f).status.ok);
    REQUIRE_FALSE(core::createNode("helix::cube", "a/b", 0.f, 0.f).status.ok);
    REQUIRE_FALSE(core::createNode("helix::nothing", "", 0.f, 0.f).status.ok);
    REQUIRE(core::listNodes().size() == 1);

    // Left unnamed, the engine picks a free name.
    REQUIRE(core::createNode("helix::cube", "", 0.f, 0.f).status.ok);
    REQUIRE(core::createNode("helix::cube", "", 0.f, 0.f).status.ok);
    const std::vector<core::NodeInfo> nodes = core::listNodes();
    REQUIRE(nodes.size() == 3);
    REQUIRE(nodes[1].name == "cube1");
    REQUIRE(nodes[2].name == "cube2");
}

TEST_CASE_METHOD(CoreFixture, "A link that would close a loop or reach no port is refused")
{
    const core::NodeId first = core::createNode("helix::transform", "a", 0.f, 0.f).id;
    const core::NodeId second = core::createNode("helix::transform", "b", 0.f, 0.f).id;

    REQUIRE(core::connectNodes(first, 0, second, 0).ok);
    REQUIRE_FALSE(core::connectNodes(second, 0, first, 0).ok);
    REQUIRE_FALSE(core::connectNodes(first, 0, first, 0).ok);
    REQUIRE_FALSE(core::connectNodes(first, 5, second, 0).ok);
    REQUIRE_FALSE(core::connectNodes(first, 0, second, 7).ok);
    REQUIRE_FALSE(core::connectNodes(first, 0, 9999, 0).ok);
    REQUIRE(core::listLinks().size() == 1);
}

TEST_CASE_METHOD(CoreFixture, "Cooking without a display node fails with a message")
{
    REQUIRE(core::createNode("helix::cube", "", 0.f, 0.f).status.ok);

    const core::GeometrySummary summary = core::cookDisplaySummary();
    REQUIRE_FALSE(summary.status.ok);
    REQUIRE_FALSE(summary.status.error.empty());
    REQUIRE_FALSE(core::cookDisplayBuffers().status.ok);
    REQUIRE_FALSE(core::setDisplayNode(9999).ok);
}

TEST_CASE_METHOD(CoreFixture, "A scene keeps the values it saved when a node changes its defaults")
{
    TempFolder folder;

    // What an attribute noise started with before it began by moving the geometry. A scene
    // writes every value of every parameter, so it reads them back as they were saved.
    const core::NodeId noise = core::createNode("helix::attributeNoise", "bruit", 0.f, 0.f).id;
    REQUIRE(core::setString(noise, "name", 0, "noise").ok);
    REQUIRE(core::setString(noise, "type", 0, "float").ok);
    REQUIRE(core::setFloat(noise, "amplitude", 0, 1.f).ok);
    REQUIRE(core::setInt(noise, "alongVector", 0, 0).ok);

    const fs::path file = folder.file("old.kut");
    REQUIRE(core::saveScene(file.string()).ok);
    REQUIRE(core::newScene().ok);
    REQUIRE(core::openScene(file.string()).ok);

    const core::NodeId reopened = core::listNodes().front().id;
    CHECK(core::getString(reopened, "name", 0).value == "noise");
    CHECK(core::getString(reopened, "type", 0).value == "float");
    CHECK(core::getFloat(reopened, "amplitude", 0).value == 1.f);
}

TEST_CASE_METHOD(CoreFixture, "A saved scene reopens as the same graph")
{
    TempFolder folder;

    const core::NodeId cube = core::createNode("helix::cube", "box", 10.f, 20.f).id;
    const core::NodeId mover = core::createNode("helix::transform", "mover", 30.f, 40.f).id;
    REQUIRE(core::connectNodes(cube, 0, mover, 0).ok);
    REQUIRE(core::setVector3(cube, "size", 2.f, 3.f, 4.f).ok);

    const fs::path file = folder.file("scene.kut");
    REQUIRE(core::saveScene(file.string()).ok);
    REQUIRE(fs::exists(file));

    // The ids are handed out again on opening, and the order they come in is not the order
    // they were saved in, so the nodes are matched by their path.
    const auto byPath = [](std::vector<core::NodeInfo> nodes) {
        std::sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) {
            return a.path < b.path;
        });
        return nodes;
    };

    const std::vector<core::NodeInfo> savedNodes = byPath(core::listNodes());
    REQUIRE(core::newScene().ok);
    REQUIRE(core::listNodes().empty());
    REQUIRE(core::openScene(file.string()).ok);

    const std::vector<core::NodeInfo> openedNodes = byPath(core::listNodes());
    REQUIRE(openedNodes.size() == savedNodes.size());
    for (std::size_t index = 0; index < openedNodes.size(); ++index)
    {
        REQUIRE(openedNodes[index].type == savedNodes[index].type);
        REQUIRE(openedNodes[index].name == savedNodes[index].name);
        REQUIRE(openedNodes[index].path == savedNodes[index].path);
        REQUIRE(openedNodes[index].x == savedNodes[index].x);
        REQUIRE(openedNodes[index].y == savedNodes[index].y);
    }

    const std::vector<core::LinkInfo> links = core::listLinks();
    REQUIRE(links.size() == 1);
    const auto nameOf = [&](core::NodeId id) {
        for (const core::NodeInfo& node : openedNodes)
            if (node.id == id) return node.name;
        return std::string();
    };
    REQUIRE(nameOf(links[0].sourceNode) == "box");
    REQUIRE(nameOf(links[0].targetNode) == "mover");

    const auto reopenedCube = std::find_if(openedNodes.begin(), openedNodes.end(), [](const auto& node) {
        return node.name == "box";
    });
    REQUIRE(reopenedCube != openedNodes.end());
    const core::Vector3Value size = core::getVector3(reopenedCube->id, "size");
    REQUIRE(size.status.ok);
    REQUIRE(size.x == 2.f);
    REQUIRE(size.y == 3.f);
    REQUIRE(size.z == 4.f);
}

TEST_CASE_METHOD(CoreFixture, "The modified state follows edits, saves and opens")
{
    TempFolder folder;
    REQUIRE_FALSE(core::isSceneModified());

    const core::NodeId cube = core::createNode("helix::cube", "", 0.f, 0.f).id;
    REQUIRE(core::isSceneModified());

    const fs::path file = folder.file("scene.kut");
    REQUIRE(core::saveScene(file.string()).ok);
    REQUIRE_FALSE(core::isSceneModified());

    REQUIRE(core::setFloat(cube, "size", 0, 2.f).ok);
    REQUIRE(core::isSceneModified());

    REQUIRE(core::openScene(file.string()).ok);
    REQUIRE_FALSE(core::isSceneModified());

    REQUIRE(core::moveNode(core::listNodes().front().id, 1.f, 1.f).ok);
    REQUIRE(core::isSceneModified());
    REQUIRE(core::newScene().ok);
    REQUIRE_FALSE(core::isSceneModified());
}

TEST_CASE_METHOD(CoreFixture, "Saving leaves no temporary file behind and replaces the old scene")
{
    TempFolder folder;
    const fs::path file = folder.file("scene.kut");

    REQUIRE(core::createNode("helix::cube", "", 0.f, 0.f).status.ok);
    REQUIRE(core::saveScene(file.string()).ok);
    const std::string first = readFile(file);

    REQUIRE(core::createNode("helix::cube", "", 0.f, 0.f).status.ok);
    REQUIRE(core::saveScene(file.string()).ok);
    REQUIRE(readFile(file) != first);

    std::size_t files = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(folder.path))
        ++files;
    REQUIRE(files == 1);
}

TEST_CASE_METHOD(CoreFixture, "A save that cannot be written leaves the previous file whole")
{
    TempFolder folder;
    const fs::path file = folder.file("scene.kut");

    REQUIRE(core::createNode("helix::cube", "", 0.f, 0.f).status.ok);
    REQUIRE(core::saveScene(file.string()).ok);
    const std::string original = readFile(file);

    REQUIRE(core::createNode("helix::cube", "", 0.f, 0.f).status.ok);

    SECTION("a folder that does not exist")
    {
        const fs::path missing = folder.path / "missing" / "scene.kut";
        const core::Status status = core::saveScene(missing.string());
        REQUIRE_FALSE(status.ok);
        REQUIRE_FALSE(status.error.empty());
        REQUIRE_FALSE(fs::exists(missing.parent_path()));
        REQUIRE(core::isSceneModified());
    }

    SECTION("a folder that cannot be written to")
    {
        // The permission bits mean nothing to the superuser.
        if (::geteuid() == 0) SKIP("running as the superuser");

        fs::permissions(folder.path, fs::perms::owner_read | fs::perms::owner_exec);
        const core::Status status = core::saveScene(file.string());
        fs::permissions(folder.path, fs::perms::owner_all);

        REQUIRE_FALSE(status.ok);
        REQUIRE(readFile(file) == original);
        REQUIRE(core::isSceneModified());

        std::size_t files = 0;
        for ([[maybe_unused]] const auto& entry : fs::directory_iterator(folder.path))
            ++files;
        REQUIRE(files == 1);
    }

    SECTION("a destination that is a folder")
    {
        const fs::path directory = folder.file("scenes");
        fs::create_directory(directory);
        REQUIRE_FALSE(core::saveScene(directory.string()).ok);
        REQUIRE(fs::is_directory(directory));
    }
}

TEST_CASE_METHOD(CoreFixture, "Opening a missing or broken file reports an error")
{
    TempFolder folder;
    REQUIRE(core::createNode("helix::cube", "", 0.f, 0.f).status.ok);

    // A file that does not exist leaves the scene as it was.
    const core::Status missing = core::openScene(folder.file("nope.kut").string());
    REQUIRE_FALSE(missing.ok);
    REQUIRE_FALSE(missing.error.empty());
    REQUIRE(core::listNodes().size() == 1);

    // A file that is not a scene is reported, and no exception escapes.
    const fs::path broken = folder.file("broken.kut");
    std::ofstream(broken) << "this is not a scene";
    const core::Status brokenStatus = core::openScene(broken.string());
    REQUIRE_FALSE(brokenStatus.ok);

    // The status carries the message of the exception the engine threw, not the fallback of
    // the guard for an exception it cannot name. That fallback appears when
    // `catch (const std::exception&)` misses what a library of the engine throws.
    REQUIRE(brokenStatus.error.find("Couldn't read") != std::string::npos);
    REQUIRE(brokenStatus.error.find("unknown reason") == std::string::npos);
}

TEST_CASE_METHOD(CoreFixture, "A damaged 3D file is reported with the reason, not an unknown failure")
{
    TempFolder folder;
    const fs::path damaged = folder.file("damaged.glb");
    std::ofstream(damaged) << "glTF this is not a binary glTF container";

    const core::CreateNodeResult result = core::importFile(damaged.string());
    REQUIRE_FALSE(result.status.ok);
    REQUIRE_FALSE(result.status.error.empty());
    REQUIRE(result.status.error.find("unknown reason") == std::string::npos);
}

TEST_CASE_METHOD(CoreFixture, "A failure inside the engine reports the exception message")
{
    TempFolder folder;
    REQUIRE(core::createNode("helix::cube", "", 0.f, 0.f).status.ok);

    // Saving into a folder that is read only makes the serializer throw a std::runtime_error
    // from inside libhelixEngine.
    fs::permissions(folder.path, fs::perms::owner_read | fs::perms::owner_exec);
    const core::Status saved = core::saveScene(folder.file("scene.kut").string());
    REQUIRE_FALSE(saved.ok);
    REQUIRE_FALSE(saved.error.empty());
    REQUIRE(saved.error.find("unknown reason") == std::string::npos);
}

TEST_CASE_METHOD(CoreFixture, "Time reports the frame, the range and the rate")
{
    core::TimeInfo time = core::timeInfo();
    REQUIRE(time.startFrame == 1);
    REQUIRE(time.endFrame > time.startFrame);
    REQUIRE(time.fps > 0.f);

    core::setFrame(5.f);
    REQUIRE(core::timeInfo().frame == 5.f);

    // A frame outside the range is clamped into it.
    core::setFrame(1.0e6f);
    REQUIRE(core::timeInfo().frame == static_cast<float>(core::timeInfo().endFrame));
    REQUIRE_FALSE(core::isSceneModified());
}

TEST_CASE_METHOD(CoreFixture, "The facade cooks on the calling thread even after background cooking was on")
{
    helix::nt::nm().setAsyncCookingEnabled(true);

    createDisplayedCube();
    REQUIRE_FALSE(helix::nt::nm().isAsyncCookingEnabled());

    const core::GeometrySummary summary = core::cookDisplaySummary();
    REQUIRE(summary.status.ok);
    REQUIRE(summary.pointCount == 8);
}

namespace {

const core::AttributeInfo* findAttribute(
    const core::GeometryReport& report,
    core::AttributeOwner owner,
    const std::string& name
)
{
    for (const core::AttributeInfo& info : report.attributes)
        if (info.owner == owner && info.name == name) return &info;
    return nullptr;
}

} // namespace

TEST_CASE_METHOD(CoreFixture, "A report on any node gives its counts, bounds and attributes")
{
    const core::NodeId shown = createDisplayedCube();
    const core::CreateNodeResult other = core::createNode("helix::cube", "second", 0.f, 0.f);
    REQUIRE(other.status.ok);
    REQUIRE(core::setVector3(other.id, "size", 4.f, 2.f, 6.f).ok);

    // The cook of a node that is not displayed leaves the display node alone.
    const core::GeometryReport report = core::cookNodeReport(other.id);
    REQUIRE(report.status.ok);
    REQUIRE(core::displayNode() == shown);
    REQUIRE(report.pointCount == 8);
    REQUIRE(report.faceCount == 6);
    REQUIRE(report.vertexCount == 24);
    REQUIRE(report.triangleCount == 12);
    REQUIRE(report.primitiveCount == 1);
    REQUIRE(report.bounds.valid);
    REQUIRE(report.bounds.minX == -2.f);
    REQUIRE(report.bounds.maxX == 2.f);
    REQUIRE(report.bounds.minY == -1.f);
    REQUIRE(report.bounds.maxY == 1.f);
    REQUIRE(report.bounds.minZ == -3.f);
    REQUIRE(report.bounds.maxZ == 3.f);

    const core::AttributeInfo* position =
        findAttribute(report, core::AttributeOwner::Point, "Position");
    REQUIRE(position != nullptr);
    REQUIRE(position->type == core::AttributeValueType::Vector3);
    REQUIRE(position->componentCount == 3);
    REQUIRE(position->elementCount == 8);

    // The summary of the display node agrees with the report on the same node.
    const core::GeometrySummary summary = core::cookDisplaySummary();
    const core::GeometryReport shownReport = core::cookNodeReport(shown);
    REQUIRE(shownReport.pointCount == summary.pointCount);
    REQUIRE(shownReport.faceCount == summary.faceCount);
    REQUIRE(shownReport.triangleCount == summary.triangleCount);

    REQUIRE_FALSE(core::cookNodeReport(987654).status.ok);
    REQUIRE_FALSE(core::cookNodeReport(0).status.error.empty());
}

namespace {

// A cube with a point attribute "tag" of the given type ("float" or "vector"), on its own path.
core::NodeId taggedCube(const char* path, const char* type)
{
    const core::NodeId cube = core::createNode("helix::cube", "", 0.f, 0.f).id;
    const core::NodeId tag = core::createNode("helix::attributeCreate", "", 0.f, 0.f).id;
    const core::NodeId named = core::createNode("helix::path", "", 0.f, 0.f).id;
    REQUIRE(core::setString(tag, "name", 0, "tag").ok);
    REQUIRE(core::setString(tag, "type", 0, type).ok);
    REQUIRE(core::setString(named, "path", 0, path).ok);
    REQUIRE(core::connectNodes(cube, 0, tag, 0).ok);
    REQUIRE(core::connectNodes(tag, 0, named, 0).ok);
    return named;
}

core::NodeId mergedTaggedCubes(const char* firstType, const char* secondType)
{
    const core::NodeId first = taggedCube("/a", firstType);
    const core::NodeId second = taggedCube("/b", secondType);
    const core::NodeId merge = core::createNode("helix::merge", "", 0.f, 0.f).id;
    REQUIRE(core::connectNodes(first, 0, merge, 0).ok);
    REQUIRE(core::connectNodes(second, 0, merge, 1).ok);
    return merge;
}

} // namespace

TEST_CASE_METHOD(CoreFixture, "A report lists the materials of an import once each, with their texture")
{
    const std::filesystem::path data = std::filesystem::path(HELIX_TEST_DATA_DIR) / "import";

    const core::CreateNodeResult textured = core::importFile((data / "textured-quad.glb").string());
    REQUIRE(textured.status.ok);
    REQUIRE(core::lastImportSummary().find("1 decoded") != std::string::npos);
    const core::GeometryReport report = core::cookNodeReport(textured.id);
    REQUIRE(report.status.ok);
    REQUIRE(report.materials.size() == 1);
    const core::MaterialInfo& material = report.materials.front();
    REQUIRE(material.name == "Painted");
    REQUIRE(material.baseColor[0] == Catch::Approx(0.9f));
    REQUIRE(material.baseColor[1] == Catch::Approx(0.8f));
    REQUIRE(material.baseColor[2] == Catch::Approx(0.7f));
    REQUIRE(material.baseColor[3] == Catch::Approx(1.f));
    REQUIRE(material.hasTexture);
    REQUIRE(material.textureWidth == 4);
    REQUIRE(material.textureHeight == 4);
    REQUIRE(material.textureSource.find("textured-quad.glb#embedded:") != std::string::npos);
    REQUIRE(material.missingTexture.empty());

    // Two meshes with a material each, kept apart: both are listed, once.
    const core::CreateNodeResult two = core::importFile((data / "two-materials.glb").string());
    REQUIRE(two.status.ok);
    REQUIRE(core::setInt(two.id, "mergeMeshes", 0, 0).ok);
    const core::GeometryReport apart = core::cookNodeReport(two.id);
    REQUIRE(apart.status.ok);
    REQUIRE(apart.primitiveCount == 2);
    REQUIRE(apart.materials.size() == 2);
    REQUIRE(apart.materials[0].name == "Red");
    REQUIRE(apart.materials[1].name == "Blue");
    REQUIRE_FALSE(apart.materials[0].hasTexture);
    REQUIRE(apart.materials[0].textureSource.empty());

    // A node with no material reports none.
    REQUIRE(core::cookNodeReport(createDisplayedCube()).materials.empty());
}

TEST_CASE_METHOD(CoreFixture, "A report flags an attribute whose type differs between primitives")
{
    const core::NodeId mixed = mergedTaggedCubes("float", "vector");
    const core::GeometryReport report = core::cookNodeReport(mixed);
    REQUIRE(report.status.ok);
    REQUIRE(report.primitiveCount == 2);
    const core::AttributeInfo* tag = findAttribute(report, core::AttributeOwner::Point, "tag");
    REQUIRE(tag != nullptr);
    REQUIRE(tag->elementCount == 16);
    REQUIRE(tag->mixedTypes);
    REQUIRE_FALSE(core::readAttribute(mixed, core::AttributeOwner::Point, "tag", 0, 100).status.ok);

    // The same type over two primitives is one attribute, and not flagged.
    const core::NodeId same = mergedTaggedCubes("float", "float");
    const core::GeometryReport sameReport = core::cookNodeReport(same);
    REQUIRE(sameReport.status.ok);
    REQUIRE(sameReport.primitiveCount == 2);
    bool seen = false;
    for (const core::AttributeInfo& info : sameReport.attributes)
        if (info.name == "tag")
        {
            REQUIRE(info.elementCount == 16);
            REQUIRE_FALSE(info.mixedTypes);
            seen = true;
        }
    REQUIRE(seen);
}

TEST_CASE_METHOD(CoreFixture, "An attribute is read in pages, with the total to page by")
{
    const core::NodeId cube = createDisplayedCube();
    REQUIRE(core::setVector3(cube, "size", 2.f, 2.f, 2.f).ok);

    const core::AttributeValues all =
        core::readAttribute(cube, core::AttributeOwner::Point, "Position", 0, 100);
    REQUIRE(all.status.ok);
    REQUIRE(all.type == core::AttributeValueType::Vector3);
    REQUIRE(all.componentCount == 3);
    REQUIRE(all.totalCount == 8);
    REQUIRE(all.start == 0);
    REQUIRE(all.floats.size() == 8 * 3);
    REQUIRE(all.ints.empty());
    for (const float value : all.floats)
        REQUIRE(std::abs(value) == 1.f);

    const core::AttributeValues page =
        core::readAttribute(cube, core::AttributeOwner::Point, "Position", 6, 5);
    REQUIRE(page.status.ok);
    REQUIRE(page.totalCount == 8);
    REQUIRE(page.start == 6);
    REQUIRE(page.floats.size() == 2 * 3);
    REQUIRE(std::equal(page.floats.begin(), page.floats.end(), all.floats.begin() + 6 * 3));

    // Past the end there is nothing, and that is not an error.
    const core::AttributeValues past =
        core::readAttribute(cube, core::AttributeOwner::Point, "Position", 50, 5);
    REQUIRE(past.status.ok);
    REQUIRE(past.totalCount == 8);
    REQUIRE(past.floats.empty());

    // A count that would overflow the end of the range is held to the attribute.
    const core::AttributeValues huge = core::readAttribute(
        cube, core::AttributeOwner::Point, "Position", 2, ~std::uint64_t(0)
    );
    REQUIRE(huge.status.ok);
    REQUIRE(huge.floats.size() == 6 * 3);
}

TEST_CASE_METHOD(CoreFixture, "Reading an attribute that is not there, or on a bad node, is an error")
{
    const core::NodeId cube = createDisplayedCube();

    const core::AttributeValues missing =
        core::readAttribute(cube, core::AttributeOwner::Point, "nope", 0, 10);
    REQUIRE_FALSE(missing.status.ok);
    REQUIRE(missing.status.error.find("nope") != std::string::npos);
    REQUIRE(missing.floats.empty());

    // The position belongs to the points, not to the faces.
    REQUIRE_FALSE(core::readAttribute(cube, core::AttributeOwner::Face, "Position", 0, 10).status.ok);
    REQUIRE_FALSE(core::readAttribute(424242, core::AttributeOwner::Point, "Position", 0, 10).status.ok);
}

TEST_CASE_METHOD(CoreFixture, "The display node can be cleared, and clearing none is not an error")
{
    REQUIRE(core::clearDisplayNode().ok);
    const core::NodeId cube = createDisplayedCube();
    REQUIRE(core::displayNode() == cube);
    REQUIRE(core::clearDisplayNode().ok);
    REQUIRE(core::displayNode() == 0);
    REQUIRE_FALSE(core::cookDisplaySummary().status.ok);
    REQUIRE(core::listNodes().size() == 1);
}

TEST_CASE_METHOD(CoreFixture, "A node type tells its label, tags, input ports and output count")
{
    const core::NodeTypeInfoResult cube = core::nodeTypeInfo("helix::cube");
    REQUIRE(cube.status.ok);
    REQUIRE(cube.info.name == "helix::cube");
    REQUIRE(cube.info.label == "Cube");
    // Every type the library ships says in a sentence what it does, an alias included. Types
    // another test registers (a script, a user node) are not the library's.
    for (const core::NodeTypeInfo& info : core::listNodeTypeInfos())
    {
        CAPTURE(info.name);
        if (info.name.rfind("helix::", 0) == 0) REQUIRE_FALSE(info.description.empty());
    }
    REQUIRE(core::nodeTypeInfo("helix::mountain").info.description.size() > 10);
    REQUIRE(cube.info.inputs.empty());
    REQUIRE(cube.info.outputCount == 1);

    // Two single ports, in the order their inputs run.
    const core::NodeTypeInfoResult boolean = core::nodeTypeInfo("helix::boolean");
    REQUIRE(boolean.status.ok);
    REQUIRE(boolean.info.inputs.size() == 2);
    REQUIRE(boolean.info.inputs[0].label == "A");
    REQUIRE(boolean.info.inputs[1].label == "B");
    REQUIRE_FALSE(boolean.info.inputs[1].multiInput);
    REQUIRE(std::find(boolean.info.tags.begin(), boolean.info.tags.end(), "combine") != boolean.info.tags.end());

    // One port that takes any number of inputs.
    const core::NodeTypeInfoResult merge = core::nodeTypeInfo("helix::merge");
    REQUIRE(merge.status.ok);
    REQUIRE(merge.info.inputs.size() == 1);
    REQUIRE(merge.info.inputs[0].multiInput);

    // An optional port is told apart from a required one.
    bool foundOptional = false;
    for (const core::NodeTypeInfo& info : core::listNodeTypeInfos())
        for (const core::PortInfo& port : info.inputs)
            foundOptional = foundOptional || port.optional;
    REQUIRE(foundOptional);

    // An alias answers with its own label and the ports of the type it creates.
    const core::NodeTypeInfoResult mountain = core::nodeTypeInfo("helix::mountain");
    REQUIRE(mountain.status.ok);
    REQUIRE(mountain.info.name == "helix::mountain");
    REQUIRE(mountain.info.inputs.size() == core::nodeTypeInfo("helix::attributeNoise").info.inputs.size());

    const core::NodeTypeInfoResult unknown = core::nodeTypeInfo("helix::nothing");
    REQUIRE_FALSE(unknown.status.ok);
    REQUIRE_FALSE(unknown.status.error.empty());
    REQUIRE(unknown.info.name.empty());
}

TEST_CASE_METHOD(CoreFixture, "Every listed node type has its info, in the same order")
{
    const std::vector<std::string> names = core::listNodeTypes();
    const std::vector<core::NodeTypeInfo> infos = core::listNodeTypeInfos();
    REQUIRE(infos.size() == names.size());
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        REQUIRE(infos[index].name == names[index]);
        REQUIRE_FALSE(infos[index].label.empty());
    }
}

namespace {

// Everything a scene keeps that the editor shows, by path, since the ids do not survive.
struct SceneShape
{
    std::vector<std::string> nodes; // "path type x y"
    std::vector<std::string> links; // "source.output -> target.input"
    std::string display;
};

SceneShape shapeOfScene()
{
    SceneShape shape;
    std::map<core::NodeId, std::string> paths;
    for (const core::NodeInfo& node : core::listNodes())
    {
        paths[node.id] = node.path;
        shape.nodes.push_back(
            node.path + " " + node.type + " " + std::to_string(node.x) + " " + std::to_string(node.y)
        );
    }
    for (const core::LinkInfo& link : core::listLinks())
        shape.links.push_back(
            paths[link.sourceNode] + "." + std::to_string(link.sourceOutput) + " -> " +
            paths[link.targetNode] + "." + std::to_string(link.targetInput)
        );
    std::sort(shape.nodes.begin(), shape.nodes.end());
    std::sort(shape.links.begin(), shape.links.end());
    shape.display = core::displayNode() == 0 ? "" : paths[core::displayNode()];
    return shape;
}

core::NodeId idOfPath(const std::string& path)
{
    for (const core::NodeInfo& node : core::listNodes())
        if (node.path == path) return node.id;
    return 0;
}

} // namespace

TEST_CASE_METHOD(CoreFixture, "A restored scene is the captured one, after deleting, connecting and moving")
{
    const core::NodeId cube = core::createNode("helix::cube", "box", 10.f, 20.f).id;
    const core::NodeId mover = core::createNode("helix::transform", "mover", 30.f, 150.f).id;
    const core::NodeId merge = core::createNode("helix::merge", "joined", 60.f, 300.f).id;
    REQUIRE(core::connectNodes(cube, 0, mover, 0).ok);
    REQUIRE(core::connectNodes(mover, 0, merge, 0).ok);
    REQUIRE(core::connectNodes(cube, 0, merge, 1).ok);
    REQUIRE(core::setVector3(cube, "size", 2.f, 3.f, 4.f).ok);
    REQUIRE(core::setDisplayNode(merge).ok);
    core::setFrame(5.f);

    const SceneShape before = shapeOfScene();
    const core::SceneSnapshot snapshot = core::captureScene();
    REQUIRE(snapshot.status.ok);
    REQUIRE_FALSE(snapshot.data.empty());
    // Capturing changes nothing, and it is the text a saved file holds.
    REQUIRE(shapeOfScene().nodes == before.nodes);

    REQUIRE(core::deleteNode(mover).ok);
    REQUIRE(core::connectNodes(cube, 0, merge, 0).ok);
    REQUIRE(core::moveNode(cube, -500.f, -500.f).ok);
    REQUIRE(core::setVector3(cube, "size", 9.f, 9.f, 9.f).ok);
    core::setFrame(8.f);
    REQUIRE(shapeOfScene().nodes != before.nodes);

    REQUIRE(core::restoreScene(snapshot.data).ok);
    const SceneShape after = shapeOfScene();
    REQUIRE(after.nodes == before.nodes);
    REQUIRE(after.links == before.links);
    REQUIRE(after.display == before.display);
    REQUIRE(after.display == "/joined");

    // The parameters and the time come back too, and the node is found by its path.
    const core::Vector3Value size = core::getVector3(idOfPath("/box"), "size");
    REQUIRE(size.status.ok);
    REQUIRE(size.x == 2.f);
    REQUIRE(size.y == 3.f);
    REQUIRE(size.z == 4.f);
    REQUIRE(core::timeInfo().frame == 5.f);

    // The restored scene cooks: the display node is wired again.
    REQUIRE(core::cookDisplaySummary().status.ok);

    // A second restore of the same text gives the same scene.
    REQUIRE(core::restoreScene(snapshot.data).ok);
    REQUIRE(shapeOfScene().links == before.links);
}

TEST_CASE_METHOD(CoreFixture, "Restoring leaves the modified state as it was")
{
    TempFolder folder;
    createDisplayedCube();
    const fs::path file = folder.file("scene.kut");
    REQUIRE(core::saveScene(file.string()).ok);
    REQUIRE_FALSE(core::isSceneModified());

    const core::SceneSnapshot saved = core::captureScene();
    REQUIRE(saved.status.ok);
    REQUIRE_FALSE(core::isSceneModified());
    REQUIRE(core::restoreScene(saved.data).ok);
    REQUIRE_FALSE(core::isSceneModified());

    REQUIRE(core::moveNode(core::listNodes().front().id, 5.f, 5.f).ok);
    REQUIRE(core::isSceneModified());
    REQUIRE(core::restoreScene(saved.data).ok);
    REQUIRE(core::isSceneModified());
}

TEST_CASE_METHOD(CoreFixture, "A scene that cannot be restored is reported, and a restore does not keep the engine's undo")
{
    createDisplayedCube();
    const core::Status broken = core::restoreScene("this is not a scene");
    REQUIRE_FALSE(broken.ok);
    REQUIRE_FALSE(broken.error.empty());
    REQUIRE(core::listNodes().empty());

    // The facade leaves nothing on the engine's own stack, which only the old editor reads.
    const core::NodeId cube = core::createNode("helix::cube", "", 0.f, 0.f).id;
    REQUIRE(core::moveNode(cube, 3.f, 4.f).ok);
    REQUIRE_FALSE(helix::nt::nm().undoStack().canUndo());
}

// A save off the main thread trusts sceneRevision to tell whether the text it holds is still the
// scene, so every call that changes what captureScene writes has to change it.
TEST_CASE_METHOD(CoreFixture, "Every call that changes the scene changes its revision", "[core][revision]")
{
    const core::NodeId torus = core::createNode("helix::torus", "", 0.f, 0.f).id;
    const core::NodeId cube = core::createNode("helix::cube", "", 200.f, 0.f).id;
    const core::NodeId sweep = core::createNode("helix::sweep", "", 400.f, 0.f).id;
    const core::NodeId box = core::createNode("helix::cube", "", 600.f, 0.f).id;
    REQUIRE(box != 0);
    REQUIRE(torus != 0);
    REQUIRE(cube != 0);
    REQUIRE(sweep != 0);
    REQUIRE(core::setKeyframe(torus, "radius", 0, 1.f, 2.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframe(torus, "radius", 0, 20.f, 4.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setDisplayNode(torus).ok);
    const std::string capture = core::captureScene().data;
    TempFolder folder;
    const std::string saved = folder.file("revision.kut").string();
    REQUIRE(core::saveScene(saved).ok);
    const std::string model = (fs::path(__FILE__).parent_path() / "data/import/textured-quad.glb").string();
    const core::NodeId imported = core::importFile(model).id;
    REQUIRE(imported != 0);

    const std::vector<std::pair<std::string, std::function<bool()>>> changes = {
        {"createNode", [&] { return core::createNode("helix::grid", "", 0.f, 300.f).status.ok; }},
        {"deleteNode", [&] { return core::deleteNode(cube).ok; }},
        {"connectNodes", [&] { return core::connectNodes(torus, 0, sweep, 0).ok; }},
        {"disconnectNodes", [&] { return core::disconnectNodes(torus, 0, sweep, 0).ok; }},
        {"moveNode", [&] { return core::moveNode(torus, 5.f, 6.f).ok; }},
        {"setDisplayNode", [&] { return core::setDisplayNode(sweep).ok; }},
        {"clearDisplayNode", [&] { return core::clearDisplayNode().ok; }},
        {"importFile", [&] { return core::importFile(model).status.ok; }},
        {"setFloat", [&] { return core::setFloat(torus, "radius", 1, 3.f).ok; }},
        {"setInt", [&] { return core::setInt(torus, "columns", 0, 11).ok; }},
        {"setString", [&] { return core::setString(imported, "filePath", 0, model + ".other").ok; }},
        {"setVector3", [&] { return core::setVector3(box, "size", 4.f, 4.f, 4.f).ok; }},
        {"setExpression", [&] { return core::setExpression(torus, "uniformScale", 0, "2 + 3").ok; }},
        {"clearExpression", [&] { return core::clearExpression(torus, "uniformScale", 0).ok; }},
        {"resetParameter", [&] { return core::resetParameter(torus, "columns").ok; }},
        {"setRamp", [&] {
             return core::setRamp(sweep, "scaleRamp", {{0.f, 1.f, "linear"}, {0.5f, 2.f, "bspline"}, {1.f, 1.f, "linear"}}).ok;
         }},
        {"setKeyframe", [&] { return core::setKeyframe(torus, "radius", 0, 8.f, 9.f, core::Interpolation::Linear).ok; }},
        {"setKeyframeAtCurrentValue", [&] { return core::setKeyframeAtCurrentValue(torus, "radius", 0, 12.f).ok; }},
        {"moveKeyframe", [&] { return core::moveKeyframe(torus, "radius", 0, 8.f, 9.f).ok; }},
        {"setKeyframeInterpolation", [&] {
             return core::setKeyframeInterpolation(torus, "radius", 0, 9.f, core::Interpolation::Step).ok;
         }},
        {"removeKeyframe", [&] { return core::removeKeyframe(torus, "radius", 0, 9.f).ok; }},
        {"clearKeyframes", [&] { return core::clearKeyframes(torus, "radius", 0).ok; }},
        {"setFrameRange", [&] { return core::setFrameRange(1, 77).ok; }},
        {"setFps", [&] { return core::setFps(30.f).ok; }},
        {"setFrame", [&] { core::setFrame(7.f); return true; }},
        {"restoreScene", [&] { return core::restoreScene(capture).ok; }},
        {"openScene", [&] { return core::openScene(saved).ok; }},
        {"newScene", [&] { return core::newScene().ok; }},
    };

    for (const auto& [name, change] : changes)
    {
        const std::uint64_t before = core::sceneRevision();
        INFO(name);
        REQUIRE(change());
        REQUIRE(core::sceneRevision() > before);
    }
}

TEST_CASE_METHOD(CoreFixture, "The revision does not move for a call that changes nothing", "[core][revision]")
{
    const core::NodeId cube = core::createNode("helix::cube", "", 0.f, 0.f).id;
    const std::uint64_t before = core::sceneRevision();
    REQUIRE_FALSE(core::setFloat(cube, "size", 9, 1.f).ok);
    REQUIRE_FALSE(core::deleteNode(99999).ok);
    (void)core::captureScene();
    (void)core::listNodes();
    REQUIRE(core::sceneRevision() == before);
}

TEST_CASE_METHOD(CoreFixture, "Warming the import cache from another thread makes the import that follows read nothing")
{
    TempFolder folder;
    const fs::path file = folder.path / "warmed.glb";
    fs::copy_file(fs::path(HELIX_TEST_DATA_DIR) / "import" / "textured-quad.glb", file);

    const std::size_t reads = helix::import::importReadCount();
    core::Status warmed;
    std::thread worker([&] { warmed = core::warmImportCache(file.string()); });
    worker.join();
    REQUIRE(warmed.ok);
    REQUIRE(helix::import::importReadCount() == reads + 1);
    // Warming makes no node: the scene is as it was.
    REQUIRE(core::listNodes().empty());

    const core::CreateNodeResult imported = core::importFile(file.string());
    REQUIRE(imported.status.ok);
    REQUIRE(helix::import::importReadCount() == reads + 1);
    REQUIRE(core::listNodes().size() == 1);
}

TEST_CASE_METHOD(CoreFixture, "The report names the textures a model refers to and cannot find")
{
    TempFolder folder;
    const fs::path data = fs::path(HELIX_TEST_DATA_DIR) / "import";
    fs::copy_file(data / "textured-quad.gltf", folder.path / "textured-quad.gltf");
    fs::copy_file(data / "textured-quad.bin", folder.path / "textured-quad.bin");

    const core::CreateNodeResult imported = core::importFile((folder.path / "textured-quad.gltf").string());
    REQUIRE(imported.status.ok);
    const core::GeometryReport report = core::cookNodeReport(imported.id);
    REQUIRE(report.status.ok);
    REQUIRE(report.materials.size() == 1);
    REQUIRE(report.materials.front().missingTextureName == "quadrants.png");
    REQUIRE(report.materials.front().missingTextureNotFound);

    // Nothing is missing for a file that carries its picture.
    const core::CreateNodeResult whole = core::importFile((data / "textured-quad.glb").string());
    REQUIRE(whole.status.ok);
    const core::GeometryReport complete = core::cookNodeReport(whole.id);
    REQUIRE(complete.materials.front().missingTextureName.empty());
}

TEST_CASE_METHOD(CoreFixture, "Reloading an import reads the file again, and only that file")
{
    TempFolder folder;
    const fs::path file = folder.path / "reload.glb";
    const fs::path other = folder.path / "other.glb";
    fs::copy_file(fs::path(HELIX_TEST_DATA_DIR) / "import" / "textured-quad.glb", file);
    fs::copy_file(fs::path(HELIX_TEST_DATA_DIR) / "import" / "textured-quad.glb", other);

    const core::NodeId node = core::importFile(file.string()).id;
    const core::NodeId second = core::importFile(other.string()).id;
    REQUIRE(core::cookNodeReport(node).status.ok);
    REQUIRE(core::cookNodeReport(second).status.ok);

    // Cooking again reads nothing: the file is as it was.
    const std::size_t reads = helix::import::importReadCount();
    REQUIRE(core::reloadImport(node).ok == true);
    REQUIRE(core::cookNodeReport(node).status.ok);
    REQUIRE(helix::import::importReadCount() == reads + 1);

    // The other file was kept: it is served from the cache.
    REQUIRE(core::reloadImport(node).ok);
    REQUIRE(core::cookNodeReport(second).status.ok);
    REQUIRE(helix::import::importReadCount() == reads + 1);
}

TEST_CASE_METHOD(CoreFixture, "Reloading refuses what is not an import and ignores an import with no file")
{
    const core::NodeId cube = core::createNode("helix::cube", "", 0.f, 0.f).id;
    REQUIRE_FALSE(core::reloadImport(cube).ok);
    REQUIRE_FALSE(core::reloadImport(99999).ok);

    const core::NodeId empty = core::createNode("helix::geometryImport", "", 0.f, 0.f).id;
    REQUIRE(core::reloadImport(empty).ok);
}

TEST_CASE_METHOD(CoreFixture, "The file path of an import is a parameter of the file style, and the Blender one is not")
{
    const core::NodeId node = core::createNode("helix::geometryImport", "", 0.f, 0.f).id;
    std::string pathStyle = "unset", blenderStyle = "unset";
    for (const core::ParameterInfo& parameter : core::listParameters(node))
    {
        if (parameter.name == "filePath") pathStyle = parameter.style;
        if (parameter.name == "blenderPath") blenderStyle = parameter.style;
    }
    REQUIRE(pathStyle == "file");
    REQUIRE(blenderStyle.empty());
}

TEST_CASE_METHOD(CoreFixture, "Warming the import cache says why a file cannot be read, as importFile does")
{
    TempFolder folder;
    const core::Status missing = core::warmImportCache((folder.path / "absent.glb").string());
    REQUIRE_FALSE(missing.ok);
    REQUIRE(missing.error == core::importFile((folder.path / "absent.glb").string()).status.error);
    REQUIRE_FALSE(core::warmImportCache("").ok);
}
