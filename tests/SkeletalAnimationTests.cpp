#include "Engine/Attribute/AttributeHandle.h"
#include "Engine/Attribute/AttributeNames.h"
#include "Engine/Core/Types.h"
#include "Engine/Import/ImportFile.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeLoader.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Primitives/Skeleton.h"
#include "HelixCore/HelixCore.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// Skeletal animation: what the import keeps of a rig, and the two nodes that pose it and skin a mesh
// to it. The files are the ones tests/data/import/make_test_files.py writes: a two joint arm, a tube
// whose bottom row follows the root joint, whose top row follows the child joint (the elbow, one unit
// above the root) and whose middle row follows both equally.

namespace fs = std::filesystem;
namespace core = helix::core;
using namespace helix;
using Catch::Approx;

namespace {

constexpr float kEpsilon = 1e-4f;
constexpr float kFps = 24.f;
constexpr float kHalfRoot = 0.70710678f;

fs::path dataFile(const std::string& name) { return fs::path(HELIX_TEST_DATA_DIR) / "import" / name; }

struct NMReset
{
    NMReset() { nt::nm()._reset(); }
    ~NMReset() { nt::nm()._reset(); }
};

// The frame of the scene that sits at a time in seconds.
float frameAt(float seconds) { return 1.f + seconds * kFps; }

bool near(const Vector3& a, const Vector3& b, float tolerance = kEpsilon) { return (a - b).cwiseAbs().maxCoeff() <= tolerance; }
bool near(const Matrix4& a, const Matrix4& b, float tolerance = kEpsilon) { return (a - b).cwiseAbs().maxCoeff() <= tolerance; }

std::shared_ptr<const import::ImportedFile> read(const std::string& name, import::ImportOptions options = {})
{
    import::clearImportCache();
    return import::importFile(dataFile(name), options);
}

std::vector<Vector3> pointsOf(const geo::Mesh& mesh)
{
    const auto span = mesh.pointPosSpan();
    return {span.begin(), span.end()};
}

template <typename T> std::vector<T> pointAttribute(const geo::Mesh& mesh, const std::string& name)
{
    const auto attribute = mesh.getAttribByName(attr::AttributeOwner::POINT, name);
    REQUIRE(attribute != nullptr);
    const attr::AttributeHandleRO<T> handle(attribute);
    std::vector<T> values;
    for (std::size_t i = 0; i < handle.getSize(); ++i)
        values.push_back(handle.getValue(i));
    return values;
}

// geometryImport -> skeletonPose (its second output) -> skinDeform, the wiring the docs describe.
struct Rig
{
    nt::NodeId import = 0;
    nt::NodeId pose = 0;
    nt::NodeId deform = 0;

    explicit Rig(const std::string& file, float size = 1.f, const std::string& upAxis = "auto", bool merge = true)
    {
        nt::NodeLoader::loadNodes();
        auto& nm = nt::nm();
        import = nm.createNode("helix::geometryImport");
        nm.getNode(import).getParameter("filePath").lock()->setString(dataFile(file).string());
        nm.getNode(import).getParameter("size").lock()->setFloat(size);
        nm.getNode(import).getParameter("upAxis").lock()->setString(upAxis);
        nm.getNode(import).getParameter("mergeMeshes").lock()->setInt(merge ? 1 : 0);
        pose = nm.createNode("helix::skeletonPose");
        deform = nm.createNode("helix::skinDeform");
        nm.connectNodes(import, 1, pose, 0);
        nm.connectNodes(import, 0, deform, 0);
        nm.connectNodes(pose, 0, deform, 1);
        // Hold keeps the pose at the end of a clip, which is what the tests of a single key want.
        setString(pose, "loop", "hold");
    }

    static void setString(nt::NodeId node, const std::string& name, const std::string& value)
    {
        nt::nm().getNode(node).getParameter(name).lock()->setString(value);
    }
    static void setFloat(nt::NodeId node, const std::string& name, float value)
    {
        nt::nm().getNode(node).getParameter(name).lock()->setFloat(value);
    }

    // Cooks the scene at a time in seconds and returns the node's first mesh.
    std::shared_ptr<const geo::Mesh> at(nt::NodeId node, float seconds, unsigned output = 0)
    {
        auto& nm = nt::nm();
        nm.setFrame(frameAt(seconds));
        nm.cook(node);
        const auto packet = nm.getNode(node).getOutputPacket(output);
        REQUIRE(packet != nullptr);
        REQUIRE(packet->size() >= 1);
        const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(packet->getPrimitive(0));
        REQUIRE(mesh != nullptr);
        return mesh;
    }

    std::vector<Vector3> skinned(float seconds) { return pointsOf(*at(deform, seconds)); }
    std::string error(nt::NodeId node) { return nt::nm().getNode(node).getError(); }
};

// The row of the tube a point of the bind pose sits on: its height, 0, 1 or 2. The import numbers the
// points in the order the faces use them, not the order the file lists them, so a point is found by
// where it is rather than by its index.
float rowOf(const Vector3& bindPosition) { return bindPosition.y(); }

// The index of the point at a place, or -1 when there is none.
int pointAt(const std::vector<Vector3>& points, const Vector3& place)
{
    for (std::size_t i = 0; i < points.size(); ++i)
        if (near(points[i], place, 1e-5f)) return static_cast<int>(i);
    return -1;
}

// Where the elbow bend of an angle (radians, about z, around the child joint at (0, 1, 0)) takes a
// point of the bind pose, for a weight on the child joint.
Vector3 bentAbout(const Vector3& point, float angle, const Vector3& pivot = Vector3(0, 1, 0))
{
    return pivot + Eigen::AngleAxisf(angle, Vector3::UnitZ()) * (point - pivot);
}

} // namespace

TEST_CASE_METHOD(NMReset, "A rigged glTF imports a skeleton whose points are the joints", "[skeleton]")
{
    for (const char* name : {"skinned-arm.glb", "skinned-arm.gltf"})
    {
        CAPTURE(name);
        const auto imported = read(name);
        REQUIRE(imported->skeleton != nullptr);
        const geo::Mesh& skeleton = *imported->skeleton;
        REQUIRE(skeleton.getNumPoints() == 2);

        const auto table = skeleton.skeleton();
        REQUIRE(table != nullptr);
        REQUIRE(table->jointNames == std::vector<std::string>{"Root", "Child"});

        // One bone, an open face from the parent to the child.
        REQUIRE(skeleton.getNumFaces() == 1);
        REQUIRE_FALSE(skeleton.isClosed(0));
        const auto bone = skeleton.getFacePoints(0);
        REQUIRE(bone.size() == 2);
        REQUIRE(bone[0] == 0);
        REQUIRE(bone[1] == 1);

        const auto positions = pointsOf(skeleton);
        REQUIRE(near(positions[0], Vector3(0, 0, 0)));
        REQUIRE(near(positions[1], Vector3(0, 1, 0)));

        const auto parents = pointAttribute<intT>(skeleton, attr::names::jointParent);
        REQUIRE(parents == std::vector<intT>{-1, 0});

        // The transforms are attributes: rest_local relative to the parent, inverse_bind from the file,
        // and pose, the rest world transform.
        const auto rest = pointAttribute<Matrix4>(skeleton, attr::names::restLocal);
        Matrix4 childLocal = Matrix4::Identity();
        childLocal(1, 3) = 1.f;
        REQUIRE(near(rest[0], Matrix4::Identity()));
        REQUIRE(near(rest[1], childLocal));
        const auto inverseBind = pointAttribute<Matrix4>(skeleton, attr::names::inverseBind);
        Matrix4 childInverse = Matrix4::Identity();
        childInverse(1, 3) = -1.f;
        REQUIRE(near(inverseBind[0], Matrix4::Identity()));
        REQUIRE(near(inverseBind[1], childInverse));
        const auto pose = pointAttribute<Matrix4>(skeleton, attr::names::pose);
        REQUIRE(near(pose[0], Matrix4::Identity()));
        REQUIRE(near(pose[1], childLocal));

        // Two clips, Bend (linear) and Snap (stepped), one second each, with a rotation channel on the child.
        REQUIRE(table->clips.size() == 2);
        REQUIRE(table->clips[0]->name == "Bend");
        REQUIRE(table->clips[0]->duration == Approx(1.f));
        // Assimp also hands over the rest translation and scale of an animated node as one key each,
        // so the child has more than the rotation channel the file animates.
        REQUIRE(table->clips[0]->channelCount() >= 1);
        REQUIRE(table->clips[0]->joints[1].rotation.times == std::vector<float>{0.f, 1.f});
        REQUIRE(table->clips[0]->joints[1].rotation.interpolation == geo::Interpolation::Linear);
        REQUIRE(table->clips[0]->joints[0].any() == false);
        REQUIRE(table->clips[1]->name == "Snap");
        REQUIRE(table->clips[1]->joints[1].rotation.interpolation == geo::Interpolation::Step);
        REQUIRE(table->clips[1]->joints[1].rotation.times == std::vector<float>{0.f, 1.f});

        REQUIRE(imported->summary.jointCount == 2);
        REQUIRE(imported->summary.rootJointCount == 1);
        REQUIRE(imported->summary.clips.size() == 2);
        const std::string text = imported->summary.text();
        CAPTURE(text);
        REQUIRE_THAT(text, Catch::Matchers::ContainsSubstring("Skeleton: 2 joints"));
        REQUIRE_THAT(text, Catch::Matchers::ContainsSubstring("Bend 1.00 s"));
        REQUIRE_THAT(text, Catch::Matchers::ContainsSubstring("Snap 1.00 s"));
        REQUIRE_THAT(text, !Catch::Matchers::ContainsSubstring("bind pose only"));
    }
}

TEST_CASE_METHOD(NMReset, "The mesh of a rigged file carries four weighted influences per point", "[skeleton]")
{
    const auto imported = read("skinned-arm.glb");
    REQUIRE(imported->meshes.size() == 1);
    const geo::Mesh& mesh = *imported->meshes[0];
    REQUIRE(mesh.getNumPoints() == 12);

    std::vector<std::vector<intT>> joints;
    std::vector<std::vector<float>> weights;
    for (int i = 0; i < attr::names::skinInfluenceCount; ++i)
    {
        joints.push_back(pointAttribute<intT>(mesh, attr::names::skinJoint[i]));
        weights.push_back(pointAttribute<floatT>(mesh, attr::names::skinWeight[i]));
    }
    for (std::size_t point = 0; point < 12; ++point)
    {
        CAPTURE(point);
        float total = 0.f;
        for (int i = 0; i < 4; ++i)
            total += weights[i][point];
        REQUIRE(total == Approx(1.f));
        // Unused influences are joint 0 with weight 0.
        REQUIRE(weights[2][point] == 0.f);
        REQUIRE(weights[3][point] == 0.f);
        REQUIRE(joints[2][point] == 0);
        REQUIRE(joints[3][point] == 0);

        const float row = rowOf(mesh.getPointPos(point));
        if (row == 0.f)
        {
            REQUIRE(joints[0][point] == 0);
            REQUIRE(weights[0][point] == Approx(1.f));
        }
        else if (row == 1.f)
        {
            REQUIRE(weights[0][point] == Approx(0.5f));
            REQUIRE(weights[1][point] == Approx(0.5f));
            REQUIRE(joints[0][point] + joints[1][point] == 1);
        }
        else
        {
            REQUIRE(joints[0][point] == 1);
            REQUIRE(weights[0][point] == Approx(1.f));
        }
    }
}

TEST_CASE_METHOD(NMReset, "The import node puts the skeleton on its second output, empty when the file has none", "[skeleton]")
{
    Rig rig("skinned-arm.glb");
    auto& nm = nt::nm();
    nm.cook(rig.import);
    REQUIRE(nm.getNode(rig.import).getOutputPacket(0)->size() == 1);
    const auto skeleton = std::dynamic_pointer_cast<const geo::Mesh>(nm.getNode(rig.import).getOutputPacket(1)->getPrimitive(0));
    REQUIRE(skeleton != nullptr);
    REQUIRE(skeleton->getNumPoints() == 2);

    // A file with no rig has an empty skeleton, and the pose node says so instead of failing.
    Rig plain("cube.glb");
    nm.cook(plain.import);
    const auto none = std::dynamic_pointer_cast<const geo::Mesh>(nm.getNode(plain.import).getOutputPacket(1)->getPrimitive(0));
    REQUIRE(none != nullptr);
    REQUIRE(none->getNumPoints() == 0);
    nm.cook(plain.pose);
    REQUIRE(plain.error(plain.pose).empty());
    REQUIRE(nm.getNode(plain.pose).getWarnings().size() == 1);
}

TEST_CASE_METHOD(NMReset, "At the first frame of a clip the pose is the rest pose", "[skeleton]")
{
    Rig rig("skinned-arm.glb");
    const auto posed = rig.at(rig.pose, 0.f);
    const auto rest = pointAttribute<Matrix4>(*read("skinned-arm.glb")->skeleton, attr::names::pose);
    const auto pose = pointAttribute<Matrix4>(*posed, attr::names::pose);
    REQUIRE(pose.size() == 2);
    REQUIRE(near(pose[0], rest[0]));
    REQUIRE(near(pose[1], rest[1]));
    const auto positions = pointsOf(*posed);
    REQUIRE(near(positions[0], Vector3(0, 0, 0)));
    REQUIRE(near(positions[1], Vector3(0, 1, 0)));
}

TEST_CASE_METHOD(NMReset, "A skin in the rest pose reproduces the input exactly", "[skeleton]")
{
    Rig rig("skinned-arm.glb");
    const auto input = pointsOf(*read("skinned-arm.glb")->meshes[0]);
    const auto output = rig.skinned(0.f);
    REQUIRE(output.size() == input.size());
    for (std::size_t point = 0; point < input.size(); ++point)
    {
        CAPTURE(point);
        REQUIRE(near(output[point], input[point], 1e-5f));
    }
}

TEST_CASE_METHOD(NMReset, "A linear clip bends the elbow and the skin follows", "[skeleton]")
{
    Rig rig("skinned-arm.glb");
    rig.setString(rig.pose, "clip", "Bend");
    const auto input = pointsOf(*read("skinned-arm.glb")->meshes[0]);

    const float quarterTurn = static_cast<float>(M_PI) / 2.f;
    for (const float seconds : {0.5f, 1.0f})
    {
        // Linear between 0 and 90 degrees over the second: half way is 45 degrees.
        const float angle = quarterTurn * seconds;
        CAPTURE(seconds);
        const auto output = rig.skinned(seconds);
        for (std::size_t point = 0; point < input.size(); ++point)
        {
            CAPTURE(point);
            const float row = rowOf(input[point]);
            // The bottom row follows the root, which does not move. The top row follows the child,
            // which turns about its own joint. The middle row is half way between the two.
            const Vector3 child = bentAbout(input[point], angle);
            const Vector3 expected = row == 0.f ? input[point] : row == 2.f ? child : Vector3((input[point] + child) * 0.5f);
            REQUIRE(near(output[point], expected));
        }
    }

    // The numbers the geometry says, by hand: the top corner (-0.5, 2, -0.5) turned a quarter about
    // the elbow lands at (-1, 0.5, -0.5), and the half way corner is at 45 degrees.
    const int corner = pointAt(input, Vector3(-0.5f, 2.f, -0.5f));
    REQUIRE(corner >= 0);
    const auto bent = rig.skinned(1.f);
    REQUIRE(near(bent[corner], Vector3(-1.f, 0.5f, -0.5f)));
    const auto half = rig.skinned(0.5f);
    REQUIRE(near(half[corner], Vector3(-kHalfRoot * 1.5f, 1.f + kHalfRoot * 0.5f, -0.5f)));

    // The posed skeleton moved with it: the child joint is the elbow, still one unit above the root.
    const auto posed = rig.at(rig.pose, 1.f);
    REQUIRE(near(pointsOf(*posed)[1], Vector3(0, 1, 0)));
    const auto pose = pointAttribute<Matrix4>(*posed, attr::names::pose);
    REQUIRE(near(Matrix4(pose[1]), [&] {
        Matrix4 expected = Matrix4::Identity();
        expected.topLeftCorner<3, 3>() = Eigen::AngleAxisf(quarterTurn, Vector3::UnitZ()).toRotationMatrix();
        expected(1, 3) = 1.f;
        return expected;
    }()));
}

TEST_CASE_METHOD(NMReset, "A stepped clip holds a pose until its next key", "[skeleton]")
{
    Rig rig("skinned-arm.glb");
    rig.setString(rig.pose, "clip", "Snap");
    const auto input = pointsOf(*read("skinned-arm.glb")->meshes[0]);

    // Held on the first key, whatever the time before the second one.
    for (const float seconds : {0.f, 0.5f, 0.99f})
    {
        CAPTURE(seconds);
        const auto output = rig.skinned(seconds);
        for (std::size_t point = 0; point < input.size(); ++point)
            REQUIRE(near(output[point], input[point], 1e-5f));
    }
    // Then the turn happens all at once, and holds.
    const int corner = pointAt(input, Vector3(-0.5f, 2.f, -0.5f));
    REQUIRE(corner >= 0);
    const auto jumped = rig.skinned(1.f);
    REQUIRE(near(jumped[corner], Vector3(-1.f, 0.5f, -0.5f)));
    REQUIRE(near(rig.skinned(1.5f)[corner], Vector3(-1.f, 0.5f, -0.5f)));
}

TEST_CASE_METHOD(NMReset, "Loop, hold and once decide what a clip does past its end", "[skeleton]")
{
    Rig rig("skinned-arm.glb");
    rig.setString(rig.pose, "clip", "Bend");

    rig.setString(rig.pose, "loop", "loop");
    const auto atQuarter = rig.skinned(0.25f);
    const auto looped = rig.skinned(1.25f);
    for (std::size_t point = 0; point < atQuarter.size(); ++point)
        REQUIRE(near(looped[point], atQuarter[point], 2e-4f));
    // A time before the start wraps from the end (the offset reaches below zero, which a frame cannot).
    rig.setFloat(rig.pose, "offset", -0.75f);
    const auto before = rig.skinned(0.f);
    for (std::size_t point = 0; point < atQuarter.size(); ++point)
        REQUIRE(near(before[point], atQuarter[point], 2e-4f));
    rig.setFloat(rig.pose, "offset", 0.f);

    rig.setString(rig.pose, "loop", "hold");
    const auto atEnd = rig.skinned(1.f);
    const auto held = rig.skinned(1.25f);
    for (std::size_t point = 0; point < atEnd.size(); ++point)
        REQUIRE(near(held[point], atEnd[point]));

    // Once plays through, then goes back to the rest pose.
    rig.setString(rig.pose, "loop", "once");
    const auto input = pointsOf(*read("skinned-arm.glb")->meshes[0]);
    const auto playing = rig.skinned(0.5f);
    const int corner = pointAt(input, Vector3(-0.5f, 2.f, -0.5f));
    REQUIRE_FALSE(near(playing[corner], input[corner], 0.1f));
    const auto done = rig.skinned(1.25f);
    for (std::size_t point = 0; point < input.size(); ++point)
        REQUIRE(near(done[point], input[point], 1e-5f));
}

TEST_CASE_METHOD(NMReset, "Speed and offset move the clip time", "[skeleton]")
{
    Rig rig("skinned-arm.glb");
    rig.setString(rig.pose, "clip", "Bend");
    const auto reference = rig.skinned(0.5f);

    rig.setFloat(rig.pose, "speed", 2.f);
    const auto doubled = rig.skinned(0.25f);
    for (std::size_t point = 0; point < reference.size(); ++point)
        REQUIRE(near(doubled[point], reference[point]));

    rig.setFloat(rig.pose, "speed", 1.f);
    rig.setFloat(rig.pose, "offset", 0.25f);
    const auto shifted = rig.skinned(0.25f);
    for (std::size_t point = 0; point < reference.size(); ++point)
        REQUIRE(near(shifted[point], reference[point]));

    // A speed of 0 holds the pose of the offset, whatever the frame.
    rig.setFloat(rig.pose, "speed", 0.f);
    rig.setFloat(rig.pose, "offset", 0.5f);
    const auto still = rig.skinned(0.9f);
    for (std::size_t point = 0; point < reference.size(); ++point)
        REQUIRE(near(still[point], reference[point]));

    // Backward: a speed of -1 at 0.5 s plays the clip at -0.5 s, which loops to 0.5 s.
    rig.setFloat(rig.pose, "speed", -1.f);
    rig.setFloat(rig.pose, "offset", 0.f);
    rig.setString(rig.pose, "loop", "loop");
    const auto backward = rig.skinned(0.5f);
    for (std::size_t point = 0; point < reference.size(); ++point)
        REQUIRE(near(backward[point], reference[point], 2e-4f));
}

TEST_CASE_METHOD(NMReset, "A frame reached directly gives the pose of one reached by playing", "[skeleton]")
{
    Rig rig("skinned-arm.glb");
    rig.setString(rig.pose, "loop", "loop");
    rig.setString(rig.pose, "clip", "Bend");
    auto& nm = nt::nm();

    nm.setFrame(37.f);
    nm.cook(rig.deform);
    const auto direct = pointsOf(*std::dynamic_pointer_cast<const geo::Mesh>(nm.getNode(rig.deform).getOutputPacket(0)->getPrimitive(0)));

    nm.setFrame(1.f);
    nm.cook(rig.deform);
    for (int frame = 1; frame <= 36; ++frame)
    {
        nm.setFrame(static_cast<float>(frame));
        nm.cook(rig.deform);
    }
    nm.setFrame(37.f);
    nm.cook(rig.deform);
    const auto played = pointsOf(*std::dynamic_pointer_cast<const geo::Mesh>(nm.getNode(rig.deform).getOutputPacket(0)->getPrimitive(0)));

    REQUIRE(direct.size() == played.size());
    for (std::size_t point = 0; point < direct.size(); ++point)
    {
        // Bit for bit: no state is kept between the cooks.
        REQUIRE(direct[point].x() == played[point].x());
        REQUIRE(direct[point].y() == played[point].y());
        REQUIRE(direct[point].z() == played[point].z());
    }
    // And the pose really did move, so the comparison is not of two rest poses.
    const auto bind = pointsOf(*read("skinned-arm.glb")->meshes[0]);
    const int corner = pointAt(bind, Vector3(-0.5f, 2.f, -0.5f));
    REQUIRE_FALSE(near(direct[corner], bind[corner], 0.05f));
}

TEST_CASE_METHOD(NMReset, "Normals follow the skin", "[skeleton]")
{
    Rig rig("skinned-arm.glb");
    rig.setString(rig.pose, "clip", "Bend");
    const auto input = read("skinned-arm.glb")->meshes[0];
    const auto bent = rig.at(rig.deform, 1.f);

    const auto before = std::dynamic_pointer_cast<const geo::Mesh>(input);
    const auto normalsBefore = before->getAttribByName(attr::AttributeOwner::VERTEX, attr::names::normal);
    const auto normalsAfter = bent->getAttribByName(attr::AttributeOwner::VERTEX, attr::names::normal);
    REQUIRE(normalsBefore != nullptr);
    REQUIRE(normalsAfter != nullptr);
    const attr::AttributeHandleRO<Vector3> was(normalsBefore);
    const attr::AttributeHandleRO<Vector3> now(normalsAfter);
    REQUIRE(was.getSize() == now.getSize());
    const auto vertexPoints = bent->vertexPointSpan();
    for (std::size_t vertex = 0; vertex < now.getSize(); ++vertex)
    {
        CAPTURE(vertex);
        REQUIRE(now[vertex].norm() == Approx(1.f).margin(1e-4f));
        const std::size_t point = static_cast<std::size_t>(vertexPoints[vertex]);
        const float row = rowOf(input->getPointPos(point));
        if (row == 2.f)
            // A normal of the top row turns with the child joint: a quarter about z.
            REQUIRE(near(now[vertex], Eigen::AngleAxisf(static_cast<float>(M_PI) / 2.f, Vector3::UnitZ()) * was[vertex]));
        else if (row == 0.f)
            REQUIRE(near(now[vertex], was[vertex]));
    }

    // The same cook with the option off leaves them where they were, and still moves the points.
    nt::nm().getNode(rig.deform).getParameter("normals").lock()->setInt(0);
    const auto still = rig.at(rig.deform, 1.f);
    const attr::AttributeHandleRO<Vector3> kept(still->getAttribByName(attr::AttributeOwner::VERTEX, attr::names::normal));
    for (std::size_t vertex = 0; vertex < kept.getSize(); ++vertex)
        REQUIRE(near(kept[vertex], was[vertex], 1e-6f));
    const int corner = pointAt(pointsOf(*input), Vector3(-0.5f, 2.f, -0.5f));
    REQUIRE_FALSE(near(still->getPointPos(corner), input->getPointPos(corner), 0.1f));
}

TEST_CASE_METHOD(NMReset, "An unknown clip name is an error that lists the clips", "[skeleton]")
{
    Rig rig("skinned-arm.glb");
    rig.setString(rig.pose, "clip", "Dance");
    nt::nm().cook(rig.pose);
    const std::string error = rig.error(rig.pose);
    CAPTURE(error);
    REQUIRE_THAT(error, Catch::Matchers::ContainsSubstring("no clip named 'Dance'"));
    REQUIRE_THAT(error, Catch::Matchers::ContainsSubstring("'Bend'"));
    REQUIRE_THAT(error, Catch::Matchers::ContainsSubstring("'Snap'"));

    // Empty plays the first clip.
    rig.setString(rig.pose, "clip", "");
    nt::nm().cook(rig.pose);
    REQUIRE(rig.error(rig.pose).empty());
}

TEST_CASE_METHOD(NMReset, "The deform refuses what it cannot skin", "[skeleton]")
{
    auto& nm = nt::nm();
    Rig rig("skinned-arm.glb");

    SECTION("a joint index outside the skeleton")
    {
        const nt::NodeId mark = nm.createNode("helix::attributeCreate");
        nm.connectNodes(rig.import, 0, mark, 0);
        nm.getNode(mark).getParameter("name").lock()->setString(attr::names::skinJoint[0]);
        nm.getNode(mark).getParameter("type").lock()->setString("int");
        nm.getNode(mark).getParameter("intValue").lock()->setInt(9);
        nm.connectNodes(mark, 0, rig.deform, 0);
        nm.cook(rig.deform);
        const std::string error = rig.error(rig.deform);
        CAPTURE(error);
        REQUIRE_THAT(error, Catch::Matchers::ContainsSubstring("joint 9"));
        REQUIRE_THAT(error, Catch::Matchers::ContainsSubstring("2 joints"));
        REQUIRE(nm.getNode(rig.deform).getOutputPacket(0)->size() == 0);
    }

    SECTION("a mesh with no skin")
    {
        const nt::NodeId cube = nm.createNode("helix::cube");
        nm.connectNodes(cube, 0, rig.deform, 0);
        nm.cook(rig.deform);
        REQUIRE_THAT(rig.error(rig.deform), Catch::Matchers::ContainsSubstring("has no skin"));
    }

    SECTION("a second input that is not a posed skeleton")
    {
        const nt::NodeId cube = nm.createNode("helix::cube");
        nm.connectNodes(cube, 0, rig.deform, 1);
        nm.cook(rig.deform);
        REQUIRE_THAT(rig.error(rig.deform), Catch::Matchers::ContainsSubstring("not a posed skeleton"));
    }

    SECTION("a missing skeleton")
    {
        const nt::NodeId alone = nm.createNode("helix::skinDeform");
        nm.connectNodes(rig.import, 0, alone, 0);
        nm.cook(alone);
        REQUIRE_THAT(rig.error(alone), Catch::Matchers::ContainsSubstring("second input"));
    }

    SECTION("a pose node fed with plain geometry")
    {
        const nt::NodeId cube = nm.createNode("helix::cube");
        nm.connectNodes(cube, 0, rig.pose, 0);
        nm.cook(rig.pose);
        REQUIRE_THAT(rig.error(rig.pose), Catch::Matchers::ContainsSubstring("not a skeleton"));
    }
}

TEST_CASE_METHOD(NMReset, "Merged meshes keep valid joint indices, and a mesh without a skin stays put", "[skeleton]")
{
    auto& nm = nt::nm();

    SECTION("merged into one mesh")
    {
        Rig rig("skinned-two-meshes.glb");
        rig.setString(rig.pose, "clip", "Bend");
        const auto imported = read("skinned-two-meshes.glb");
        REQUIRE(imported->meshes.size() == 1);
        const geo::Mesh& merged = *imported->meshes[0];
        REQUIRE(merged.getNumPoints() == 36);
        REQUIRE(imported->skeleton->getNumPoints() == 2);

        const auto jointOfFirst = pointAttribute<intT>(merged, attr::names::skinJoint[0]);
        const auto jointOfSecond = pointAttribute<intT>(merged, attr::names::skinJoint[1]);
        const auto weight = pointAttribute<floatT>(merged, attr::names::skinWeight[0]);
        for (std::size_t point = 0; point < 36; ++point)
        {
            REQUIRE(jointOfFirst[point] >= 0);
            REQUIRE(jointOfFirst[point] < 2);
            REQUIRE(jointOfSecond[point] >= 0);
            REQUIRE(jointOfSecond[point] < 2);
        }
        // The tube that is not skinned (the last twelve points) has no weight anywhere.
        for (std::size_t point = 24; point < 36; ++point)
            REQUIRE(weight[point] == 0.f);

        const auto input = pointsOf(merged);
        const auto output = rig.skinned(1.f);
        REQUIRE(output.size() == 36);
        for (std::size_t point = 0; point < 36; ++point)
        {
            CAPTURE(point);
            if (point >= 24)
            {
                // A point with no weight is not moved, and it is not collapsed to the origin.
                REQUIRE(near(output[point], input[point], 1e-6f));
                continue;
            }
            const float row = rowOf(input[point]);
            const Vector3 child = bentAbout(input[point], static_cast<float>(M_PI) / 2.f);
            const Vector3 expected = row == 0.f ? input[point] : row == 2.f ? child : Vector3((input[point] + child) * 0.5f);
            REQUIRE(near(output[point], expected));
        }
        // The static tube sits at the node translation (0, 5, 0), so it really was placed by its node.
        REQUIRE(near(input[24], Vector3(-0.5f, 5.f, -0.5f)));
    }

    SECTION("kept as three meshes")
    {
        Rig rig("skinned-two-meshes.glb", 1.f, "auto", false);
        rig.setString(rig.pose, "clip", "Bend");
        nm.setFrame(frameAt(1.f));
        nm.cook(rig.deform);
        REQUIRE(rig.error(rig.deform).empty());
        const auto packet = nm.getNode(rig.deform).getOutputPacket(0);
        REQUIRE(packet->size() == 3);
        // The mesh that has no skin passes through, with a note.
        REQUIRE(nm.getNode(rig.deform).getWarnings().size() == 1);
        const auto prop = std::dynamic_pointer_cast<const geo::Mesh>(packet->getPrimitive(2));
        REQUIRE(near(prop->getPointPos(0), Vector3(-0.5f, 5.f, -0.5f), 1e-6f));
        const auto armB = std::dynamic_pointer_cast<const geo::Mesh>(packet->getPrimitive(1));
        // The first point of the second tube is on its bottom row, which follows the root.
        REQUIRE(near(armB->getPointPos(0), Vector3(2.5f, 0.f, -0.5f), 1e-5f));
        const int top = pointAt(pointsOf(*armB), bentAbout(Vector3(2.5f, 2.f, -0.5f), static_cast<float>(M_PI) / 2.f));
        REQUIRE(top >= 0);
    }
}

TEST_CASE_METHOD(NMReset, "Joints under a moved and turned armature node pose the mesh correctly", "[skeleton]")
{
    Rig rig("skinned-arm-armature.glb");
    rig.setString(rig.pose, "clip", "Bend");
    const auto imported = read("skinned-arm-armature.glb");
    REQUIRE(imported->skeleton != nullptr);

    // The armature node is not a joint, so the root carries its transform above it.
    const auto rootBase = imported->skeleton->skeleton()->rootBase;
    REQUIRE(rootBase.size() == 2);
    REQUIRE(near(rootBase[0].col(3).head<3>().eval(), Vector3(5, 0, 0)));
    const auto positions = pointsOf(*imported->skeleton);
    REQUIRE(near(positions[0], Vector3(5, 0, 0)));
    REQUIRE(near(positions[1], Vector3(5, 1, 0)));

    const auto input = pointsOf(*imported->meshes[0]);
    // At the start of the clip the skin changes nothing.
    const auto rest = rig.skinned(0.f);
    for (std::size_t point = 0; point < input.size(); ++point)
        REQUIRE(near(rest[point], input[point], 1e-5f));

    // The elbow turns about the local z of the child, which the armature turned onto the world x.
    const auto bent = rig.skinned(1.f);
    const Vector3 pivot(5, 1, 0);
    for (std::size_t point = 0; point < input.size(); ++point)
    {
        CAPTURE(point);
        const float row = rowOf(input[point]);
        const Vector3 child = pivot + Eigen::AngleAxisf(static_cast<float>(M_PI) / 2.f, Vector3::UnitX()) * (input[point] - pivot);
        const Vector3 expected = row == 0.f ? input[point] : row == 2.f ? child : Vector3((input[point] + child) * 0.5f);
        REQUIRE(near(bent[point], expected));
    }
}

TEST_CASE_METHOD(NMReset, "More than four influences keep the four strongest, and weights are normalized", "[skeleton]")
{
    const auto imported = read("skinned-six-influences.glb");
    REQUIRE(imported->skeleton != nullptr);
    REQUIRE(imported->skeleton->getNumPoints() == 6);
    // A chain: each joint is the child of the one before.
    REQUIRE(pointAttribute<intT>(*imported->skeleton, attr::names::jointParent) == std::vector<intT>{-1, 0, 1, 2, 3, 4});
    REQUIRE(imported->summary.overInfluencedVertices == 1);
    REQUIRE_THAT(imported->summary.text(), Catch::Matchers::ContainsSubstring("1 vertex has more than 4 influences"));

    const geo::Mesh& mesh = *imported->meshes[0];
    REQUIRE(mesh.getNumPoints() == 3);
    const auto positions = pointsOf(mesh);
    const int first = pointAt(positions, Vector3(0, 0, 0));
    const int second = pointAt(positions, Vector3(1, 0, 0));
    const int third = pointAt(positions, Vector3(0, 1, 0));
    REQUIRE(first >= 0);
    REQUIRE(second >= 0);
    REQUIRE(third >= 0);

    std::vector<std::vector<intT>> joints;
    std::vector<std::vector<floatT>> weights;
    for (int i = 0; i < 4; ++i)
    {
        joints.push_back(pointAttribute<intT>(mesh, attr::names::skinJoint[i]));
        weights.push_back(pointAttribute<floatT>(mesh, attr::names::skinWeight[i]));
    }
    // Six influences: the 0.35, 0.25, 0.15 and 0.10 stay (joints 0 to 3, strongest first) and share what is
    // left of the weight, 0.85, between them.
    const float kept[4] = {0.35f / 0.85f, 0.25f / 0.85f, 0.15f / 0.85f, 0.10f / 0.85f};
    for (int i = 0; i < 4; ++i)
    {
        CAPTURE(i);
        REQUIRE(joints[i][static_cast<std::size_t>(first)] == i);
        REQUIRE(weights[i][static_cast<std::size_t>(first)] == Approx(kept[i]));
    }
    // One joint with a weight of 0.5 is brought up to 1.
    REQUIRE(joints[0][static_cast<std::size_t>(second)] == 3);
    REQUIRE(weights[0][static_cast<std::size_t>(second)] == Approx(1.f));
    for (int i = 1; i < 4; ++i)
        REQUIRE(weights[i][static_cast<std::size_t>(second)] == 0.f);
    // A vertex with no weight has none, and is not moved by the skin.
    for (int i = 0; i < 4; ++i)
        REQUIRE(weights[i][static_cast<std::size_t>(third)] == 0.f);

    // This file has no clip: the pose node warns and holds the rest pose, and the skin still works.
    Rig rig("skinned-six-influences.glb");
    const auto rest = rig.skinned(0.f);
    REQUIRE(rig.error(rig.pose).empty());
    REQUIRE(nt::nm().getNode(rig.pose).getWarnings().size() == 1);
    for (std::size_t point = 0; point < positions.size(); ++point)
        REQUIRE(near(rest[point], positions[point], 1e-5f));
}

TEST_CASE_METHOD(NMReset, "The node that holds a skinned mesh does not place it, its skeleton does", "[skeleton]")
{
    // The node of this tube is moved 5 up, which glTF ignores for a skinned mesh.
    const auto imported = read("skinned-arm-moved-node.glb");
    const auto points = pointsOf(*imported->meshes[0]);
    for (const Vector3& point : points)
    {
        REQUIRE(point.y() >= -1e-6f);
        REQUIRE(point.y() <= 2.f + 1e-6f);
    }

    // So the skin at rest leaves it where it is, and the elbow still bends about the joint.
    Rig rig("skinned-arm-moved-node.glb");
    rig.setString(rig.pose, "clip", "Bend");
    const auto rest = rig.skinned(0.f);
    for (std::size_t point = 0; point < points.size(); ++point)
        REQUIRE(near(rest[point], points[point], 1e-5f));
    const int top = pointAt(points, Vector3(-0.5f, 2.f, -0.5f));
    REQUIRE(top >= 0);
    REQUIRE(near(rig.skinned(1.f)[top], Vector3(-1.f, 0.5f, -0.5f)));
}

TEST_CASE_METHOD(NMReset, "The import's size scales the skeleton, the clips and the skin together", "[skeleton]")
{
    Rig unit("skinned-arm.glb");
    unit.setString(unit.pose, "clip", "Bend");
    const auto reference = unit.skinned(1.f);
    const auto referenceRest = unit.skinned(0.f);

    Rig doubled("skinned-arm.glb", 2.f);
    doubled.setString(doubled.pose, "clip", "Bend");
    const auto bent = doubled.skinned(1.f);
    const auto rest = doubled.skinned(0.f);
    const auto restInput = pointsOf(*doubled.at(doubled.import, 0.f));
    for (std::size_t point = 0; point < reference.size(); ++point)
    {
        CAPTURE(point);
        REQUIRE(near(bent[point], reference[point] * 2.f));
        REQUIRE(near(rest[point], restInput[point], 1e-5f));
        REQUIRE(near(rest[point], referenceRest[point] * 2.f));
    }
    // The joints scaled too.
    REQUIRE(near(pointsOf(*doubled.at(doubled.pose, 0.f))[1], Vector3(0, 2, 0)));
}

TEST_CASE_METHOD(NMReset, "A Z up reading corrects the skeleton, the skin and the clips together", "[skeleton]")
{
    Rig upright("skinned-arm.glb");
    upright.setString(upright.pose, "clip", "Bend");
    Rig turned("skinned-arm.glb", 1.f, "z");
    turned.setString(turned.pose, "clip", "Bend");

    // z up to y up takes (x, y, z) to (x, z, -y).
    const auto correct = [](const Vector3& p) { return Vector3(p.x(), p.z(), -p.y()); };

    const auto restInput = pointsOf(*turned.at(turned.import, 0.f));
    const auto rest = turned.skinned(0.f);
    for (std::size_t point = 0; point < rest.size(); ++point)
        REQUIRE(near(rest[point], restInput[point], 1e-5f));

    const auto bentUp = upright.skinned(1.f);
    const auto bentZ = turned.skinned(1.f);
    for (std::size_t point = 0; point < bentUp.size(); ++point)
    {
        CAPTURE(point);
        REQUIRE(near(bentZ[point], correct(bentUp[point])));
    }
    // The skeleton stands inside the mesh: the child joint is where the corrected import puts it.
    REQUIRE(near(pointsOf(*turned.at(turned.pose, 0.f))[1], correct(Vector3(0, 1, 0))));
}

TEST_CASE("Sampling a track clamps outside its keys and takes the short way round", "[skeleton]")
{
    geo::ClipTrack<Vector3> position;
    position.times = {1.f, 2.f};
    position.values = {Vector3(0, 0, 0), Vector3(2, 0, 0)};
    REQUIRE(near(geo::sampleTrack(position, 0.f), Vector3(0, 0, 0)));
    REQUIRE(near(geo::sampleTrack(position, 1.5f), Vector3(1, 0, 0)));
    REQUIRE(near(geo::sampleTrack(position, 9.f), Vector3(2, 0, 0)));
    position.interpolation = geo::Interpolation::Step;
    REQUIRE(near(geo::sampleTrack(position, 1.5f), Vector3(0, 0, 0)));

    // Two keys that hold the same turn with opposite signs: the short way round is no turn at all.
    geo::ClipTrack<geo::Quaternion> rotation;
    rotation.times = {0.f, 1.f};
    rotation.values = {geo::Quaternion(1, 0, 0, 0), geo::Quaternion(-1, 0, 0, 0)};
    const geo::Quaternion half = geo::sampleTrack(rotation, 0.5f);
    REQUIRE(std::abs(half.w()) == Approx(1.f));

    geo::ClipTrack<geo::Quaternion> none;
    REQUIRE(geo::sampleTrack(none, 3.f).isApprox(geo::Quaternion::Identity()));

    // Decomposing and recomposing gives the matrix back, mirrors included.
    Matrix4 matrix = geo::composeTrs(Vector3(1, 2, 3), geo::Quaternion(Eigen::AngleAxisf(0.7f, Vector3(1, 1, 0).normalized())), Vector3(2, 1, 0.5f));
    Vector3 t, s;
    geo::Quaternion r;
    geo::decomposeTrs(matrix, t, r, s);
    REQUIRE(near(geo::composeTrs(t, r, s), matrix));
    Matrix4 mirrored = geo::composeTrs(Vector3(0, 0, 0), geo::Quaternion::Identity(), Vector3(-1, 1, 1));
    geo::decomposeTrs(mirrored, t, r, s);
    REQUIRE(near(geo::composeTrs(t, r, s), mirrored));
}

TEST_CASE("The facade reports the joints, roots and clips of a skeleton", "[skeleton]")
{
    REQUIRE(core::initialize({}).ok);
    REQUIRE(core::newScene().ok);
    const core::CreateNodeResult imported = core::importFile(dataFile("skinned-arm.glb").string());
    REQUIRE(imported.status.ok);
    const core::CreateNodeResult pose = core::createNode("helix::skeletonPose", "armPose", 0.f, 0.f);
    REQUIRE(pose.status.ok);
    REQUIRE(core::connectNodes(imported.id, 1, pose.id, 0).ok);

    const core::GeometryReport report = core::cookNodeReport(pose.id);
    REQUIRE(report.status.ok);
    REQUIRE(report.skeleton.present);
    REQUIRE(report.skeleton.jointCount == 2);
    REQUIRE(report.skeleton.rootCount == 1);
    REQUIRE(report.skeleton.clips.size() == 2);
    REQUIRE(report.skeleton.clips[0].name == "Bend");
    REQUIRE(report.skeleton.clips[0].duration == Approx(1.f));
    REQUIRE(report.skeleton.clips[1].name == "Snap");
    // The bones are lines: one open face, and no triangle to shade.
    REQUIRE(report.faceCount == 1);
    REQUIRE(report.triangleCount == 0);

    // The mesh the skin deforms is not a skeleton, and says nothing of one.
    REQUIRE_FALSE(core::cookNodeReport(imported.id).skeleton.present);
}

// What the deform costs on a real rig, measured in a Release build. Hidden from the normal run, since a
// time is not a check, and given a file by the environment so the repository needs none:
//   HELIX_BENCH_RIG=/path/to/rig.glb build-core/helix.app/Contents/bin/tests "[.bench]"
TEST_CASE("The cost of posing and skinning a rig", "[.bench]")
{
    const char* file = std::getenv("HELIX_BENCH_RIG");
    if (!file) SKIP("set HELIX_BENCH_RIG to a rigged glTF to measure it");
    REQUIRE(core::initialize({}).ok);
    REQUIRE(core::newScene().ok);
    auto& nm = nt::nm();
    const core::CreateNodeResult imported = core::importFile(file);
    REQUIRE(imported.status.ok);
    const nt::NodeId import = imported.id;
    const nt::NodeId pose = core::createNode("helix::skeletonPose", "pose", 0.f, 0.f).id;
    const nt::NodeId deform = core::createNode("helix::skinDeform", "deform", 0.f, 0.f).id;
    REQUIRE(core::connectNodes(import, 1, pose, 0).ok);
    REQUIRE(core::connectNodes(import, 0, deform, 0).ok);
    REQUIRE(core::connectNodes(pose, 0, deform, 1).ok);
    nm.cook(import);
    REQUIRE(core::setFrameRange(1, 1000).ok);

    const auto millis = [](auto&& work) {
        const auto start = std::chrono::steady_clock::now();
        work();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    for (const float frame : {1.f, 8.f, 24.f, 48.f})
    {
        double poseTotal = 0.0, deformTotal = 0.0, endToEnd = 0.0;
        constexpr int repeats = 20;
        for (int repeat = 0; repeat < repeats; ++repeat)
        {
            // Away and back, so each timed step is a cook that has to happen.
            core::setFrame(frame + 1.f);
            nm.cook(deform);
            core::setFrame(frame);
            endToEnd += millis([&] { nm.cook(deform); });
            // The two nodes alone, with the input already there.
            core::setFrame(frame + 1.f);
            nm.cook(deform);
            core::setFrame(frame);
            poseTotal += millis([&] { nm.cook(pose); });
            deformTotal += millis([&] { nm.cook(deform); });
        }
        WARN("frame " << frame << ": skeletonPose " << poseTotal / repeats << " ms, skinDeform " << deformTotal / repeats
                      << " ms, setFrame + both cooks " << endToEnd / repeats << " ms");
    }
    // And the whole frame of playback: the move, the cook, and the buffers the viewport uploads. Run after
    // the loop above, since the display cook leaves the nodes clean for the node timings.
    REQUIRE(core::setDisplayNode(deform).ok);
    for (const float frame : {1.f, 8.f, 24.f, 48.f})
    {
        constexpr int repeats = 20;
        double displayTotal = 0.0;
        for (int repeat = 0; repeat < repeats; ++repeat)
        {
            core::setFrame(frame + 1.f);
            core::cookDisplayBuffers();
            displayTotal += millis([&] {
                core::setFrame(frame);
                REQUIRE(core::cookDisplayBuffers().status.ok);
            });
        }
        WARN("frame " << frame << ": setFrame + display buffers " << displayTotal / repeats << " ms");
    }
}
