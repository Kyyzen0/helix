// What leaves the application: the obj writer of the engine and the facade calls over it
// (exportGeometry), and the queries the timeline and the skeleton chain of the interface ask
// (sceneAnimation, the clips that reach an input).

#include "Engine/Export/ObjWriter.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Simulation/KeyframeStore.h"
#include "HelixCore/HelixCore.h"
#include "TestPaths.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace core = helix::core;
namespace fs = std::filesystem;
using Catch::Approx;

namespace {

struct Fixture
{
    Fixture()
    {
        REQUIRE(core::initialize({}).ok);
        REQUIRE(core::newScene().ok);
        helix::sim::KeyframeStore::instance().clear();
    }
    ~Fixture() { helix::sim::KeyframeStore::instance().clear(); }
};

core::NodeId makeNode(const std::string& type, const std::string& name = "")
{
    const core::CreateNodeResult result = core::createNode(type, name, 0.f, 0.f);
    REQUIRE(result.status.ok);
    return result.id;
}

std::string readAll(const std::string& path)
{
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

std::string dataFile(const std::string& name) { return (fs::path(HELIX_TEST_DATA_DIR) / "import" / name).string(); }

// The positions of a node's points, three floats each.
std::vector<float> positionsOf(core::NodeId node)
{
    const core::AttributeValues values =
        core::readAttribute(node, core::AttributeOwner::Point, "Position", 0, 1'000'000);
    REQUIRE(values.status.ok);
    return values.floats;
}

// Whether a point of `positions` sits at (x, y, z).
bool hasPoint(const std::vector<float>& positions, float x, float y, float z)
{
    for (std::size_t index = 0; index + 2 < positions.size(); index += 3)
        if (positions[index] == Approx(x).margin(1e-5) && positions[index + 1] == Approx(y).margin(1e-5) &&
            positions[index + 2] == Approx(z).margin(1e-5))
            return true;
    return false;
}

} // namespace

TEST_CASE_METHOD(Fixture, "An exported cube is read back by the importer with the same faces and corners", "[export]")
{
    const core::NodeId cube = makeNode("helix::cube");
    REQUIRE(core::setDisplayNode(cube).ok);
    const core::GeometryReport original = core::cookNodeReport(cube);
    REQUIRE(original.status.ok);

    const std::string path = helix::test::tempPath("helix_export_cube.obj");
    const core::GeometryExportResult written = core::exportGeometry(path);
    REQUIRE(written.status.ok);
    REQUIRE(written.pointCount == original.pointCount);
    REQUIRE(written.faceCount == original.faceCount);
    REQUIRE(written.lineCount == 0);

    const std::string text = readAll(path);
    REQUIRE(text.find("\nvn ") != std::string::npos);
    REQUIRE(text.find("\nf ") != std::string::npos);

    // Read through the importer, the same shape comes back: its points are among the original's.
    const core::CreateNodeResult imported = core::importFile(path);
    REQUIRE(imported.status.ok);
    const core::GeometryReport again = core::cookNodeReport(imported.id);
    REQUIRE(again.status.ok);
    REQUIRE(again.faceCount == original.faceCount);
    const std::vector<float> before = positionsOf(cube);
    const std::vector<float> after = positionsOf(imported.id);
    REQUIRE_FALSE(after.empty());
    for (std::size_t index = 0; index + 2 < after.size(); index += 3)
        REQUIRE(hasPoint(before, after[index], after[index + 1], after[index + 2]));
    for (std::size_t index = 0; index + 2 < before.size(); index += 3)
        REQUIRE(hasPoint(after, before[index], before[index + 1], before[index + 2]));
    REQUIRE(again.bounds.maxX == Approx(original.bounds.maxX).margin(1e-5));
    REQUIRE(again.bounds.minY == Approx(original.bounds.minY).margin(1e-5));
    fs::remove(path);
}

TEST_CASE_METHOD(Fixture, "An export refuses a path it cannot write and leaves the file that was there", "[export]")
{
    const core::NodeId cube = makeNode("helix::cube");
    REQUIRE(core::setDisplayNode(cube).ok);

    const core::GeometryExportResult missing = core::exportGeometry("/nonexistent_helix_folder/out.obj");
    REQUIRE_FALSE(missing.status.ok);
    REQUIRE(missing.status.error.find("does not exist") != std::string::npos);

    REQUIRE_FALSE(core::exportGeometry(fs::temp_directory_path().string()).status.ok);
    REQUIRE_FALSE(core::exportGeometry("").status.ok);

    // A file already there is replaced only by a whole file.
    const std::string path = helix::test::tempPath("helix_export_keep.obj");
    {
        std::ofstream(path) << "old\n";
    }
    REQUIRE(core::exportGeometry(path).status.ok);
    REQUIRE(readAll(path).find("old") == std::string::npos);
    fs::remove(path);
}

TEST_CASE_METHOD(Fixture, "An export with nothing displayed says so", "[export]")
{
    const core::GeometryExportResult result = core::exportGeometry(helix::test::tempPath("helix_export_none.obj"));
    REQUIRE_FALSE(result.status.ok);
    REQUIRE_FALSE(fs::exists(helix::test::tempPath("helix_export_none.obj")));
}

TEST_CASE("Points alone, open faces and several meshes are written as the format wants", "[export]")
{
    helix::NodePacket packet;
    auto first = std::make_shared<helix::geo::Mesh>("/first");
    first->addPoints(std::vector<helix::Vector3>{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});
    auto second = std::make_shared<helix::geo::Mesh>("/second part");
    const auto ids = second->addPoints(std::vector<helix::Vector3>{{5, 5, 5}, {6, 5, 5}, {6, 6, 5}});
    second->addFace({ids[0], ids[1], ids[2]}, true);
    packet.addPrimitive(first);
    packet.addPrimitive(second);

    std::ostringstream out;
    const helix::exporter::ObjStats stats = helix::exporter::writeObj(packet, out);
    REQUIRE(stats.error.empty());
    REQUIRE(stats.points == 6);
    REQUIRE(stats.faces == 1);
    const std::string text = out.str();
    REQUIRE(text.find("g /first\n") != std::string::npos);
    // The name of a group has no space, since it ends at one.
    REQUIRE(text.find("g /second_part\n") != std::string::npos);
    // The face of the second mesh counts on from the first mesh's three points.
    REQUIRE(text.find("f 4//") != std::string::npos);
}

TEST_CASE("A point with no position cannot be written", "[export]")
{
    helix::NodePacket packet;
    auto mesh = std::make_shared<helix::geo::Mesh>("/bad");
    mesh->addPoints(std::vector<helix::Vector3>{{0, 0, 0}, {std::nanf(""), 0, 0}});
    packet.addPrimitive(mesh);
    std::ostringstream out;
    REQUIRE_FALSE(helix::exporter::writeObj(packet, out).error.empty());
}

TEST_CASE_METHOD(Fixture, "A scene with nothing that moves has no animation", "[animation]")
{
    makeNode("helix::cube");
    REQUIRE_FALSE(core::sceneAnimation().any());
}

TEST_CASE_METHOD(Fixture, "A key, a pose node, a simulation or a time expression makes an animation", "[animation]")
{
    const core::NodeId cube = makeNode("helix::cube");
    core::AnimationHints hints = core::sceneAnimation();
    REQUIRE_FALSE(hints.any());

    SECTION("a key")
    {
        REQUIRE(core::setKeyframe(cube, "size", 0, 1.f, 1.f, core::Interpolation::Linear).ok);
        hints = core::sceneAnimation();
        REQUIRE(hints.keyedNodes == 1);
        REQUIRE(hints.any());
    }
    SECTION("a pose node")
    {
        makeNode("helix::skeletonPose");
        REQUIRE(core::sceneAnimation().skeletonPoseNodes == 1);
    }
    SECTION("a simulation")
    {
        makeNode("helix::simulation");
        REQUIRE(core::sceneAnimation().simulationNodes == 1);
    }
    SECTION("expressions that read the time, in each spelling")
    {
        for (const char* text : {"$F * 0.1", "$FF", "$T", "sin(frame() * 0.1)", "time ()", "1 + frame( )"})
        {
            REQUIRE(core::setExpression(cube, "size", 0, text).ok);
            REQUIRE(core::sceneAnimation().timeExpressions == 1);
            REQUIRE(core::clearExpression(cube, "size", 0).ok);
            REQUIRE_FALSE(core::sceneAnimation().any());
        }
    }
    SECTION("expressions that do not read the time")
    {
        for (const char* text : {"1 + 2", "myframe(3)", "$Fx", "sin(prm(\"cube1.size\"))"})
        {
            if (!core::setExpression(cube, "size", 0, text).ok) continue;
            REQUIRE(core::sceneAnimation().timeExpressions == 0);
            REQUIRE(core::clearExpression(cube, "size", 0).ok);
        }
    }
}

TEST_CASE_METHOD(Fixture, "The clips that reach an input are the ones of the skeleton upstream", "[animation]")
{
    const core::CreateNodeResult imported = core::importFile(dataFile("skinned-arm.glb"));
    REQUIRE(imported.status.ok);
    const core::NodeId pose = makeNode("helix::skeletonPose");

    // Nothing wired: no skeleton, and no error.
    core::SkeletonClipsResult clips = core::skeletonClipsReachingInput(pose, 0);
    REQUIRE(clips.status.ok);
    REQUIRE_FALSE(clips.present);

    // The skeleton is the second output of the import.
    REQUIRE(core::connectNodes(imported.id, 1, pose, 0).ok);
    clips = core::skeletonClipsReachingInput(pose, 0);
    REQUIRE(clips.status.ok);
    REQUIRE(clips.present);
    REQUIRE(clips.clips.size() == 2);
    REQUIRE(clips.clips[0].name == "Bend");
    REQUIRE(clips.clips[0].duration == Approx(1.f));
    REQUIRE(clips.clips[1].name == "Snap");

    // The same list from the output itself, and none from the geometry output.
    const core::SkeletonClipsResult direct = core::skeletonClipsOfOutput(imported.id, 1);
    REQUIRE(direct.present);
    REQUIRE(direct.clips.size() == 2);
    REQUIRE_FALSE(core::skeletonClipsOfOutput(imported.id, 0).present);

    // Through the pose node, which passes the skeleton on.
    const core::NodeId deform = makeNode("helix::skinDeform");
    REQUIRE(core::connectNodes(pose, 0, deform, 1).ok);
    REQUIRE(core::skeletonClipsReachingInput(deform, 1).clips.size() == 2);

    // A node that is not there is an error.
    REQUIRE_FALSE(core::skeletonClipsReachingInput(987654, 0).status.ok);
}

TEST_CASE_METHOD(Fixture, "The texture coordinates of an import are written and read back", "[export]")
{
    const core::CreateNodeResult imported = core::importFile(dataFile("uvsquare.obj"));
    REQUIRE(imported.status.ok);
    REQUIRE(core::setDisplayNode(imported.id).ok);
    const core::GeometryReport original = core::cookNodeReport(imported.id);
    REQUIRE(original.status.ok);

    const std::string path = helix::test::tempPath("helix_export_uv.obj");
    const core::GeometryExportResult written = core::exportGeometry(path);
    REQUIRE(written.status.ok);
    const std::string text = readAll(path);
    REQUIRE(text.find("\nvt ") != std::string::npos);

    const core::CreateNodeResult again = core::importFile(path);
    REQUIRE(again.status.ok);
    const core::GeometryReport reread = core::cookNodeReport(again.id);
    REQUIRE(reread.status.ok);
    REQUIRE(reread.faceCount == original.faceCount);
    const auto hasUv = [](const core::GeometryReport& report) {
        return std::any_of(report.attributes.begin(), report.attributes.end(), [](const core::AttributeInfo& info) {
            return info.name == "uv";
        });
    };
    REQUIRE(hasUv(original));
    REQUIRE(hasUv(reread));
    fs::remove(path);
}
