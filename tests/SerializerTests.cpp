#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeLoader.h"
#include "Engine/Network/NodeTypeTable.h"
#include "Engine/Parameter/Parameter.h"
#include "Engine/Serializer/ParameterSerializable.h"
#include "Engine/Serializer/Serializer.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using namespace helix;

namespace {

prm::Parameter makeRamp(int pointCount)
{
    return prm::Parameter(
        prm::Template(
            prm::Type::RAMP,
            prm::Name("amplitude", "Amplitude"),
            prm::Default(pointCount)
        )
    );
}

struct NMReset
{
    NMReset()
    {
        helix::nt::NodeLoader::loadNodes();
        helix::nt::nm()._reset();
    }
    ~NMReset() { helix::nt::nm()._reset(); }
};

// Registers a node type that holds a scope and returns its full name.
std::string addScopedType()
{
    helix::nt::NodeLoader::loadNodes();
    helix::nt::NodeType nodeType = helix::nt::NodeTypeTable::requireNodeType("helix::grid");
    nodeType.internalName = "scopedContainer";
    nodeType.childScopeType = "geometry";
    return helix::nt::NodeTypeTable::addNodeType(std::move(nodeType)).getFullName();
}

const std::string scopedTypeName = addScopedType();

} // namespace

TEST_CASE("A flat parameter round trips through the serializable model")
{
    prm::Parameter source(
        prm::Template(prm::Type::FLOAT, prm::Name("frequency", "Frequency"), prm::Default(0))
    );
    source.setFloat(3.5);

    ParameterSerializable model = toSerializable(source);

    prm::Parameter restored(
        prm::Template(prm::Type::FLOAT, prm::Name("frequency", "Frequency"), prm::Default(0))
    );
    applySerializable(restored, model);

    REQUIRE(restored.evalFloat() == 3.5f);
}

TEST_CASE("A flat parameter expression round trips through the serializable model")
{
    prm::Parameter source(
        prm::Template(prm::Type::FLOAT, prm::Name("frequency", "Frequency"), prm::Default(0))
    );
    source.setExpression("5 + 5");

    ParameterSerializable model = toSerializable(source);

    prm::Parameter restored(
        prm::Template(prm::Type::FLOAT, prm::Name("frequency", "Frequency"), prm::Default(0))
    );
    applySerializable(restored, model);

    REQUIRE(restored.hasExpression());
    REQUIRE(restored.getExpression() == "5 + 5");
}

TEST_CASE("A ramp serializes its instance fields without touching the flat value")
{
    prm::Parameter source = makeRamp(2);
    source.getInstanceField(0, "position")->setFloat(0);
    source.getInstanceField(0, "value")->setFloat(2);
    source.getInstanceField(1, "position")->setFloat(1);
    source.getInstanceField(1, "value")->setFloat(8);
    source.getInstanceField(1, "interp")->setString("constant");

    ParameterSerializable model = toSerializable(source);

    REQUIRE(model.instances.size() == 2);
    REQUIRE(model.floatValues.empty());
    REQUIRE(model.intValues.empty());
}

TEST_CASE("A ramp round trips its per point position value and interpolation")
{
    prm::Parameter source = makeRamp(2);
    source.getInstanceField(0, "position")->setFloat(0);
    source.getInstanceField(0, "value")->setFloat(2);
    source.getInstanceField(1, "position")->setFloat(1);
    source.getInstanceField(1, "value")->setFloat(8);
    source.getInstanceField(1, "interp")->setString("constant");

    ParameterSerializable model = toSerializable(source);

    prm::Parameter restored = makeRamp(2);
    applySerializable(restored, model);

    REQUIRE(restored.getInstanceCount() == 2);
    REQUIRE(restored.getInstanceField(0, "value")->evalFloat() == 2);
    REQUIRE(restored.getInstanceField(1, "position")->evalFloat() == 1);
    REQUIRE(restored.getInstanceField(1, "value")->evalFloat() == 8);
    REQUIRE(restored.getInstanceField(1, "interp")->evalString() == "constant");
}

TEST_CASE("Applying a ramp model reconciles a mismatched instance count")
{
    prm::Parameter source = makeRamp(4);
    ParameterSerializable grown = toSerializable(source);

    // A freshly created ramp starts at its own default count and must resize to
    // match whatever the model carries, both up and down.
    prm::Parameter restoredUp = makeRamp(1);
    applySerializable(restoredUp, grown);
    REQUIRE(restoredUp.getInstanceCount() == 4);

    prm::Parameter single = makeRamp(1);
    ParameterSerializable shrunk = toSerializable(single);
    prm::Parameter restoredDown = makeRamp(3);
    applySerializable(restoredDown, shrunk);
    REQUIRE(restoredDown.getInstanceCount() == 1);
}

TEST_CASE_METHOD(NMReset, "A node path round trips through save and load")
{
    auto& nm = nt::nm();
    nt::NodeId grid = nm.createNode("helix::grid");
    // Use a path the placeholder would never regenerate so the test fails if the
    // path is not actually serialized
    const std::string savedPath = "/my_renamed_grid";
    nm.getNode(grid).setPath(savedPath);

    const std::string path = "/tmp/helix_serializer_path_roundtrip.json";
    nt::Serializer serializer;
    serializer.save(nm, path);

    nm._reset();
    serializer.load(nm, path);

    REQUIRE(nm.nodes().size() == 1);
    for (auto [nodeId, node] : nm.nodes())
        REQUIRE(node.getPath().getString() == savedPath);
}

TEST_CASE_METHOD(NMReset, "A node link round trips through save and load")
{
    auto& nm = nt::nm();
    nt::NodeId grid = nm.createNode("helix::grid");
    nt::NodeId transform = nm.createNode("helix::transform");
    nt::nm().connectNodes(grid, 0, transform, 0);

    const std::string path = "/tmp/helix_serializer_roundtrip.json";
    nt::Serializer serializer;
    serializer.save(nm, path);

    nm._reset();
    serializer.load(nm, path);

    // Find the reloaded nodes by type since load assigns fresh ids
    std::optional<nt::NodeId> loadedGrid;
    std::optional<nt::NodeId> loadedTransform;
    for (auto [nodeId, node] : nm.nodes())
    {
        if (node.getType().getName() == "grid") loadedGrid = nodeId;
        if (node.getType().getName() == "transform") loadedTransform = nodeId;
    }
    REQUIRE(loadedGrid.has_value());
    REQUIRE(loadedTransform.has_value());

    // The transform reads its single input from the grid
    std::vector<nt::NodeLink> inputs = nm.graph().getInputs(loadedTransform.value());
    REQUIRE(inputs.size() == 1);
    REQUIRE(inputs[0].sourceNode == loadedGrid.value());
}

TEST_CASE_METHOD(NMReset, "A node inside a scope round trips through save and load")
{
    auto& nm = nt::nm();

    nt::NodeId container = nm.createNode(scopedTypeName);
    const Path containerPath = nm.getNode(container).getPath();
    nm.createNode("helix::grid", containerPath);

    const std::string path = "/tmp/helix_serializer_scope_roundtrip.json";
    nt::Serializer serializer;
    serializer.save(nm, path);

    nm._reset();
    serializer.load(nm, path);

    // The container has to come back before the node living in it, or there is no
    // scope to place that node in.
    REQUIRE(nm.getScope(containerPath) != nullptr);
    REQUIRE(nm.getChildNodeIds(containerPath).size() == 1);
}

TEST_CASE_METHOD(NMReset, "Loading a scene updates the network once at the end")
{
    auto& nm = nt::nm();

    // A small network whose parameters and links all dirty nodes while loading
    nt::NodeId grid = nm.createNode("helix::grid");
    nt::NodeId first = nm.createNode("helix::transform");
    nt::NodeId second = nm.createNode("helix::transform");
    nm.getNode(first).getParameter("translate").lock()->setFloat(2.0f);
    nm.getNode(second).getParameter("translate").lock()->setFloat(3.0f);
    nm.connectNodes(grid, 0, first, 0);
    nm.connectNodes(first, 0, second, 0);

    const std::string path = "/tmp/helix_serializer_update_count.json";
    nt::Serializer serializer;
    serializer.save(nm, path);
    nm._reset();

    // An update refreshes the display geometry, so counting its signal counts
    // updates. The display is set when the grid appears, and the count restarts
    // there, leaving the parameters and links restored after it to be counted.
    int updates = 0;
    int lockedEvents = 0;
    int events = 0;
    bool armed = false;
    auto countUpdates = nm.displayGeoChanged.connect([&](auto) { ++updates; });
    auto armDisplay = nm.nodeCreated.connect([&](nt::NodeId nodeId) {
        ++events;
        if (nt::UpdateLock::isLocked()) ++lockedEvents;
        if (armed || nm.getNode(nodeId).getType().getName() != "grid") return;
        armed = true;
        nm.setDisplayNode(nodeId);
        updates = 0;
    });
    auto countLinks = nm.nodeLinkCreated.connect([&](nt::NodeLink) {
        ++events;
        if (nt::UpdateLock::isLocked()) ++lockedEvents;
    });

    serializer.load(nm, path);

    // The network updated once, when the load finished, not once per edit
    REQUIRE(armed);
    REQUIRE(updates == 1);
    REQUIRE(nt::UpdateLock::isUnlocked());

    // Every node and link was restored with the lock held
    REQUIRE(events == 5);
    REQUIRE(lockedEvents == events);

    // The display node was cooked by that single update, as it was before
    REQUIRE(nm.getDisplayNode().has_value());
    REQUIRE_FALSE(nm.getNode(nm.getDisplayNode().value()).isDirty());
}

TEST_CASE_METHOD(NMReset, "A failed load releases the update lock")
{
    auto& nm = nt::nm();

    const std::string path = "/tmp/helix_serializer_bad_scene.json";
    {
        std::ofstream file(path);
        file << "not a scene";
    }

    nt::Serializer serializer;
    REQUIRE_THROWS(serializer.load(nm, path));
    REQUIRE(nt::UpdateLock::isUnlocked());
}

TEST_CASE_METHOD(NMReset, "The timeline round trips through save and load")
{
    auto& nm = nt::nm();

    nm.setStartFrame(10);
    nm.setEndFrame(50);
    nm.setFps(30);
    nm.setFrame(20);

    const std::string path = "/tmp/helix_serializer_timeline_roundtrip.json";
    nt::Serializer serializer;
    serializer.save(nm, path);

    nm._reset();
    serializer.load(nm, path);

    REQUIRE(nm.getStartFrame() == 10);
    REQUIRE(nm.getEndFrame() == 50);
    REQUIRE(nm.getFps() == 30);
    REQUIRE(nm.getFrame() == 20);
}

TEST_CASE_METHOD(NMReset, "Loading a file without a timeline fails")
{
    const std::string path = "/tmp/helix_serializer_no_timeline.json";
    std::ofstream(path) << R"({"networkModel": {"nodes": [], "nodeLinks": []}})";

    nt::Serializer serializer;
    REQUIRE_THROWS(serializer.load(nt::nm(), path));
}

namespace {

std::string readFile(const std::string& path)
{
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string replaceAll(std::string text, const std::string& from, const std::string& to)
{
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
    return text;
}

// Describes a network by the type, path and wiring of its nodes, ignoring the node ids a load
// hands out.
std::vector<std::string> describeNetwork(helix::nt::NetworkManager& nm)
{
    using namespace helix;
    std::vector<std::string> lines;
    for (auto [nodeId, node] : nm.nodes())
    {
        std::string line = node.getType().getFullName() + " " + node.getPath().getString();
        for (const nt::NodeLink& link : nm.graph().getInputs(nodeId))
            line += " <-" + nm.getNode(link.sourceNode).getPath().getString() + ":" +
                    std::to_string(link.sourceOutput) + ">" + std::to_string(link.targetInput);
        lines.push_back(line);
    }
    std::sort(lines.begin(), lines.end());
    return lines;
}

} // namespace

// A scene saved before the rename to "helix" names its node types "enzo::<name>". The legacy
// file here is a real save with the type names rewritten, so the layout is the real one.
TEST_CASE_METHOD(NMReset, "A scene saved with enzo:: type names loads like the helix:: one")
{
    auto& nm = nt::nm();
    nt::NodeId grid = nm.createNode("helix::grid");
    nt::NodeId transform = nm.createNode("helix::transform");
    nm.connectNodes(grid, 0, transform, 0);
    const std::vector<std::string> expected = describeNetwork(nm);
    REQUIRE(expected.size() == 2);

    const std::string currentPath = "/tmp/helix_serializer_current_names.json";
    const std::string legacyPath = "/tmp/helix_serializer_legacy_names.json";
    const std::string resavedPath = "/tmp/helix_serializer_legacy_resaved.json";
    nt::Serializer serializer;
    serializer.save(nm, currentPath);

    const std::string legacyScene = replaceAll(readFile(currentPath), "helix::", "enzo::");
    REQUIRE(legacyScene.find("enzo::grid") != std::string::npos);
    REQUIRE(legacyScene.find("helix::") == std::string::npos);
    std::ofstream(legacyPath) << legacyScene;

    nm._reset();
    serializer.load(nm, legacyPath);
    REQUIRE(describeNetwork(nm) == expected);

    // Saving again writes only the new names
    serializer.save(nm, resavedPath);
    const std::string resaved = readFile(resavedPath);
    REQUIRE(resaved.find("enzo") == std::string::npos);
    REQUIRE(resaved.find("helix::grid") != std::string::npos);
    REQUIRE(resaved.find("helix::transform") != std::string::npos);
}
