#include "Engine/Core/Types.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeImpl.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Network/NodeType.h"
#include "Engine/Network/NodeTypeTable.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Parameter/Template.h"
#include "HelixCore/HelixCore.h"
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>

namespace core = helix::core;
namespace nt = helix::nt;
namespace prm = helix::prm;

namespace {

// A node whose cook can be told to fail in each of the ways a cook can fail, so the tests
// reach the engine's handling of them without depending on what a real node refuses.
class Troublemaker : public nt::NodeImpl
{
    using NodeImpl::NodeImpl;
    void cook() override
    {
        const int mode = evalParmInt("mode");
        // Produces something first, so a failure has an output to leave behind.
        if (hasInput(0)) setOutputPacket(0, cloneInputPacket(0));
        switch (mode)
        {
        case 1:
            throw std::runtime_error("the implementation threw");
        case 2:
            throw 42; // not a std::exception
        case 3:
            throwError("the node refused");
            return;
        case 4:
            throwWarning("something looked odd");
            return;
        case 5:
            throw std::runtime_error("");
        default:
            return;
        }
    }
};

nt::NodeImpl* makeTroublemaker(nt::Node& node, nt::CookContext& context)
{
    return new Troublemaker(node, context);
}

const std::string& troublemakerType()
{
    static const std::string name = [] {
        nt::NodeType type;
        type.internalName = "troublemaker";
        type.typeNamespace = "test";
        type.displayName = "Troublemaker";
        type.ctorFunc = &makeTroublemaker;
        type.templates = {prm::Template(prm::Type::INT, prm::Name("mode"), prm::Default(0))};
        type.inputPorts = {nt::InputPort{"in", false, true}};
        type.maxOutputs = 1;
        return nt::NodeTypeTable::addNodeType(std::move(type)).getFullName();
    }();
    return name;
}

struct Fixture
{
    Fixture()
    {
        REQUIRE(core::initialize({}).ok);
        REQUIRE(core::newScene().ok);
    }
};

core::NodeId makeNode(const std::string& type, const std::string& name = "")
{
    const core::CreateNodeResult result = core::createNode(type, name, 0.f, 0.f);
    REQUIRE(result.status.ok);
    return result.id;
}

core::NodeInfo infoOf(core::NodeId id)
{
    for (const core::NodeInfo& info : core::listNodes())
        if (info.id == id) return info;
    FAIL("no such node");
    return {};
}

} // namespace

TEST_CASE_METHOD(Fixture, "A node that calls throwError carries the error until a later cook is good", "[nodeError]")
{
    const core::NodeId cube = makeNode("helix::cube");
    const core::NodeId noise = makeNode("helix::attributeNoise", "noise");
    REQUIRE(core::connectNodes(cube, 0, noise, 0).ok);
    REQUIRE(core::setDisplayNode(noise).ok);

    // The node starts with an empty name, which it refuses.
    REQUIRE(core::setString(noise, "name", 0, "").ok);
    const core::GeometrySummary failed = core::cookDisplaySummary();
    REQUIRE_FALSE(failed.status.ok);
    REQUIRE(failed.status.error == "/noise: The attribute needs a name.");
    REQUIRE(infoOf(noise).error == "The attribute needs a name.");
    REQUIRE(infoOf(cube).error.empty());

    // The failed node leaves an empty output, not the geometry of an earlier cook.
    REQUIRE(core::cookNodeReport(noise).pointCount == 0);

    // Fixing the value is enough: the next cook starts from a clean slate.
    REQUIRE(core::setString(noise, "name", 0, "wobble").ok);
    REQUIRE(infoOf(noise).error.empty()); // dirty nodes do not keep a verdict that is out of date
    const core::GeometrySummary good = core::cookDisplaySummary();
    REQUIRE(good.status.ok);
    REQUIRE(good.pointCount == 8);
    REQUIRE(infoOf(noise).error.empty());
}

TEST_CASE_METHOD(Fixture, "A node that cooked well and then fails does not hand its old geometry downstream", "[nodeError]")
{
    const core::NodeId cube = makeNode("helix::cube");
    const core::NodeId noise = makeNode("helix::attributeNoise", "noise");
    const core::NodeId mover = makeNode("helix::transform", "mover");
    REQUIRE(core::connectNodes(cube, 0, noise, 0).ok);
    REQUIRE(core::connectNodes(noise, 0, mover, 0).ok);
    REQUIRE(core::setString(noise, "name", 0, "wobble").ok);
    REQUIRE(core::cookNodeReport(mover).status.ok);
    REQUIRE(core::cookNodeReport(mover).pointCount == 8);

    REQUIRE(core::setString(noise, "name", 0, "").ok);
    const core::GeometryReport report = core::cookNodeReport(mover);
    REQUIRE_FALSE(report.status.ok);
    // The node that is asked for is fine; the one upstream of it is why it has nothing.
    REQUIRE(report.status.error == "/noise: The attribute needs a name.");
    REQUIRE(infoOf(mover).error.empty());
    REQUIRE(infoOf(noise).error == "The attribute needs a name.");
}

TEST_CASE_METHOD(Fixture, "An exception thrown by a node is recorded on it and does not escape the cook", "[nodeError]")
{
    const core::NodeId node = makeNode(troublemakerType(), "trouble");
    const core::NodeId cube = makeNode("helix::cube");
    REQUIRE(core::connectNodes(cube, 0, node, 0).ok);
    REQUIRE(core::setDisplayNode(node).ok);

    SECTION("a std::exception gives its message")
    {
        REQUIRE(core::setInt(node, "mode", 0, 1).ok);
        helix::nt::nm().cook(node); // the engine itself must not throw
        REQUIRE(infoOf(node).error == "the implementation threw");
        REQUIRE(core::cookDisplaySummary().status.error == "/trouble: the implementation threw");
        // The output the node had set before it threw is not kept.
        REQUIRE(core::cookNodeReport(node).pointCount == 0);
    }
    SECTION("anything else, or an empty message, gets a message of its own")
    {
        REQUIRE(core::setInt(node, "mode", 0, 2).ok);
        REQUIRE(core::cookDisplaySummary().status.error == "/trouble: The node failed without a message.");
        REQUIRE(core::setInt(node, "mode", 0, 5).ok);
        REQUIRE(core::cookDisplaySummary().status.error == "/trouble: The node failed without a message.");
    }
    SECTION("the rest of the graph goes on cooking, with an empty input where the node was")
    {
        const core::NodeId after = makeNode("helix::transform", "after");
        REQUIRE(core::connectNodes(node, 0, after, 0).ok);
        REQUIRE(core::setInt(node, "mode", 0, 1).ok);
        const core::GeometryReport report = core::cookNodeReport(after);
        REQUIRE_FALSE(report.status.ok);
        REQUIRE(report.status.error == "/trouble: the implementation threw");
        REQUIRE(report.pointCount == 0);
        REQUIRE(infoOf(after).error.empty());
    }
    SECTION("a later good cook clears it")
    {
        REQUIRE(core::setInt(node, "mode", 0, 1).ok);
        REQUIRE_FALSE(core::cookDisplaySummary().status.ok);
        REQUIRE(core::setInt(node, "mode", 0, 0).ok);
        REQUIRE(core::cookDisplaySummary().status.ok);
        REQUIRE(infoOf(node).error.empty());
    }
}

TEST_CASE_METHOD(Fixture, "throwError keeps its first message, and a warning does not fail the cook", "[nodeError]")
{
    const core::NodeId node = makeNode(troublemakerType(), "trouble");
    REQUIRE(core::setDisplayNode(node).ok);

    REQUIRE(core::setInt(node, "mode", 0, 3).ok);
    REQUIRE_FALSE(core::cookDisplaySummary().status.ok);
    REQUIRE(infoOf(node).error == "the node refused");
    REQUIRE(infoOf(node).warnings.empty());

    REQUIRE(core::setInt(node, "mode", 0, 4).ok);
    const core::GeometrySummary warned = core::cookDisplaySummary();
    REQUIRE(warned.status.ok);
    REQUIRE(infoOf(node).error.empty());
    REQUIRE(infoOf(node).warnings == std::vector<std::string>{"something looked odd"});

    // The next good cook forgets the warning.
    REQUIRE(core::setInt(node, "mode", 0, 0).ok);
    REQUIRE(core::cookDisplaySummary().status.ok);
    REQUIRE(infoOf(node).warnings.empty());
}

TEST_CASE_METHOD(Fixture, "The root cause is the failing node furthest upstream", "[nodeError]")
{
    const core::NodeId first = makeNode(troublemakerType(), "first");
    const core::NodeId second = makeNode(troublemakerType(), "second");
    REQUIRE(core::connectNodes(first, 0, second, 0).ok);
    REQUIRE(core::setDisplayNode(second).ok);
    REQUIRE(core::setInt(first, "mode", 0, 3).ok);
    REQUIRE(core::setInt(second, "mode", 0, 3).ok);

    const core::GeometrySummary summary = core::cookDisplaySummary();
    REQUIRE_FALSE(summary.status.ok);
    REQUIRE(summary.status.error == "/first: the node refused");
    // Each card still says what is wrong with its own node.
    REQUIRE(infoOf(second).error == "the node refused");

    REQUIRE_FALSE(core::cookDisplayBuffers().status.ok);
    REQUIRE_FALSE(core::updateViewportGeometry().status.ok);
    REQUIRE(core::updateViewportGeometry().status.error == "/first: the node refused");
}

TEST_CASE_METHOD(Fixture, "A scene without errors reports none", "[nodeError]")
{
    const core::NodeId cube = makeNode("helix::cube");
    REQUIRE(core::setDisplayNode(cube).ok);
    REQUIRE(core::cookDisplaySummary().status.ok);
    REQUIRE(infoOf(cube).error.empty());
    REQUIRE(infoOf(cube).warnings.empty());
}
