// Renaming a node, copying and pasting nodes, and a node that remembers the alias it came from:
// what the node editor of the native app stands on, through the facade.

#include "Engine/Core/Path.h"
#include "Engine/Network/NodeReferences.h"
#include "Engine/Simulation/KeyframeStore.h"
#include "HelixCore/HelixCore.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <regex>
#include <string>
#include <unistd.h>
#include <vector>

namespace core = helix::core;
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

core::NodeId makeNode(const std::string& type, const std::string& name = "", float x = 0.f, float y = 0.f)
{
    const core::CreateNodeResult result = core::createNode(type, name, x, y);
    REQUIRE(result.status.ok);
    return result.id;
}

core::NodeInfo infoOf(core::NodeId id)
{
    for (const core::NodeInfo& info : core::listNodes())
        if (info.id == id) return info;
    FAIL("no node " << id);
    return {};
}

std::vector<std::string> namesOf()
{
    std::vector<std::string> names;
    for (const core::NodeInfo& info : core::listNodes()) names.push_back(info.name);
    std::sort(names.begin(), names.end());
    return names;
}

std::string expressionOf(core::NodeId node, const std::string& parameter, std::uint32_t component = 0)
{
    for (const core::ParameterInfo& info : core::listParameters(node))
        if (info.name == parameter) return info.expressions[component];
    FAIL("no parameter " << parameter);
    return {};
}

helix::nt::NodeRenames renaming(const std::string& from, const std::string& to) { return {{from, to}}; }

const helix::Path root("/");

// Replaces the first match of a pattern. The JSON of a copy puts a space after each colon, which
// a test that edits it should not depend on.
std::string edited(std::string text, const std::string& pattern, const std::string& with)
{
    std::smatch match;
    REQUIRE(std::regex_search(text, match, std::regex(pattern)));
    text.replace(static_cast<std::size_t>(match.position(0)), static_cast<std::size_t>(match.length(0)), with);
    return text;
}

} // namespace

TEST_CASE("A reference is rewritten as a whole name, in the form it was written", "[rename]")
{
    using helix::nt::rewriteNodeReferences;
    const auto renames = renaming("/grid1", "/ground");

    CHECK(rewriteNodeReferences("prm(\"grid1.size\") * 2", root, renames) == "prm(\"ground.size\") * 2");
    CHECK(rewriteNodeReferences("prm(\"/grid1.size\", 1)", root, renames) == "prm(\"/ground.size\", 1)");
    CHECK(rewriteNodeReferences("prmI( \"grid1.rows\" )", root, renames) == "prmI( \"ground.rows\" )");
    CHECK(rewriteNodeReferences("prmS(\"grid1.name\")", root, renames) == "prmS(\"ground.name\")");
    // The scope is read from: a reference that climbs out of a scope still lands on the node.
    CHECK(rewriteNodeReferences("prm(\"../grid1.size\")", helix::Path("/box"), renames) ==
          "prm(\"../ground.size\")");

    // grid10 is another node, and a name that only contains grid1 is not grid1.
    CHECK(rewriteNodeReferences("prm(\"grid10.size\")", root, renames) == "prm(\"grid10.size\")");
    CHECK(rewriteNodeReferences("prm(\"mygrid1.size\")", root, renames) == "prm(\"mygrid1.size\")");
    CHECK(rewriteNodeReferences("prm(\"grid1x.size\")", root, renames) == "prm(\"grid1x.size\")");

    // Only the first argument of a parameter call is a path.
    CHECK(rewriteNodeReferences("let grid1 = 3.0; grid1 + prm(\"other.size\")", root, renames) ==
          "let grid1 = 3.0; grid1 + prm(\"other.size\")");
    CHECK(rewriteNodeReferences("let name = \"grid1.size\"", root, renames) == "let name = \"grid1.size\"");
    CHECK(rewriteNodeReferences("myprm(\"grid1.size\")", root, renames) == "myprm(\"grid1.size\")");
    CHECK(rewriteNodeReferences("x.prm(\"grid1.size\")", root, renames) == "x.prm(\"grid1.size\")");

    // A comment reads nothing, and neither does a string with an escape in it.
    CHECK(rewriteNodeReferences("// prm(\"grid1.size\")\n1", root, renames) == "// prm(\"grid1.size\")\n1");
    CHECK(rewriteNodeReferences("/* prm(\"grid1.size\") */ 1", root, renames) == "/* prm(\"grid1.size\") */ 1");
    CHECK(rewriteNodeReferences("prm(\"grid1\\.size\")", root, renames) == "prm(\"grid1\\.size\")");

    // Several calls, and a string that never closes.
    CHECK(rewriteNodeReferences("prm(\"grid1.a\") + prm(\"grid1.b\")", root, renames) ==
          "prm(\"ground.a\") + prm(\"ground.b\")");
    CHECK(rewriteNodeReferences("prm(\"grid1.a", root, renames) == "prm(\"grid1.a");

    // A reference to the node itself has no node part, and follows the node.
    CHECK(rewriteNodeReferences("prm(\".size\")", root, renames) == "prm(\".size\")");
}

TEST_CASE("Names that swap are each rewritten once", "[rename]")
{
    helix::nt::NodeRenames swap{{"/a1", "/a2"}, {"/a2", "/a3"}};
    CHECK(helix::nt::rewriteNodeReferences("prm(\"a1.x\") + prm(\"a2.x\")", root, swap) ==
          "prm(\"a2.x\") + prm(\"a3.x\")");
}

TEST_CASE_METHOD(Fixture, "A node is renamed, and what reads it follows", "[rename]")
{
    const core::NodeId grid = makeNode("helix::grid");
    const core::NodeId grid10 = makeNode("helix::grid", "grid10");
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setExpression(transform, "uniform_scale", 0, "prm(\"grid1.size\") + prm(\"grid10.size\")").ok);
    REQUIRE(core::connectNodes(grid, 0, transform, 0).ok);
    REQUIRE(core::setDisplayNode(transform).ok);

    REQUIRE(core::renameNode(grid, "ground").ok);

    CHECK(infoOf(grid).name == "ground");
    CHECK(infoOf(grid).path == "/ground");
    CHECK(infoOf(grid10).name == "grid10");
    // grid10 is not touched by the rename of grid1.
    CHECK(expressionOf(transform, "uniform_scale") == "prm(\"ground.size\") + prm(\"grid10.size\")");
    CHECK(core::getFloat(transform, "uniform_scale", 0).status.ok);
    CHECK(core::listParameters(transform).size() > 0);
    // The ids, the link and the display node stay.
    REQUIRE(core::listLinks().size() == 1);
    CHECK(core::listLinks()[0].sourceNode == grid);
    CHECK(core::displayNode() == transform);
    // Both names are free to take again: the old one is gone.
    CHECK(core::renameNode(grid10, "grid1").ok);
}

TEST_CASE_METHOD(Fixture, "A script that reads a renamed node follows it", "[rename]")
{
    const core::NodeId grid = makeNode("helix::grid");
    const core::NodeId script = makeNode("helix::script");
    REQUIRE(core::setString(script, "code", 0, "@Position.y += prm(\"grid1.size\")").ok);

    REQUIRE(core::renameNode(grid, "floor").ok);

    CHECK(core::getString(script, "code", 0).value == "@Position.y += prm(\"floor.size\")");
}

TEST_CASE_METHOD(Fixture, "A renamed node keeps its keyframes", "[rename]")
{
    const core::NodeId grid = makeNode("helix::grid");
    REQUIRE(core::setKeyframe(grid, "size", 0, 1.f, 1.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframe(grid, "size", 0, 24.f, 5.f, core::Interpolation::Linear).ok);

    REQUIRE(core::renameNode(grid, "floor").ok);

    const std::vector<core::KeyframeTrackInfo> tracks = core::listKeyframes(grid);
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].keys.size() == 2);

    // The scene a save writes holds the keys under the new name.
    const core::SceneSnapshot snapshot = core::captureScene();
    REQUIRE(snapshot.status.ok);
    CHECK(snapshot.data.find("\"/floor\"") != std::string::npos);
    CHECK(snapshot.data.find("\"/grid1\"") == std::string::npos);
}

TEST_CASE_METHOD(Fixture, "A name that cannot be taken is refused and changes nothing", "[rename]")
{
    const core::NodeId grid = makeNode("helix::grid");
    makeNode("helix::cube");
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setExpression(transform, "uniform_scale", 0, "prm(\"grid1.size\")").ok);

    const core::Status taken = core::renameNode(grid, "cube1");
    CHECK_FALSE(taken.ok);
    CHECK(taken.error == "A node named 'cube1' already exists.");
    CHECK_FALSE(core::renameNode(grid, "").ok);
    CHECK_FALSE(core::renameNode(grid, "grid_angle-1").ok);
    CHECK_FALSE(core::renameNode(grid, "has space").ok);
    CHECK_FALSE(core::renameNode(grid, "/grid").ok);
    CHECK_FALSE(core::renameNode(grid, "naïve").ok);
    const std::string huge(core::maxNodeNameLength + 1, 'a');
    const core::Status tooLong = core::renameNode(grid, huge);
    CHECK_FALSE(tooLong.ok);
    // The message does not carry the name that was given.
    CHECK(tooLong.error.find(huge) == std::string::npos);
    CHECK_FALSE(core::renameNode(9999, "x").ok);

    CHECK(infoOf(grid).name == "grid1");
    CHECK(expressionOf(transform, "uniform_scale") == "prm(\"grid1.size\")");
    // The name a node has already is a success that does nothing.
    CHECK(core::renameNode(grid, "grid1").ok);
}

TEST_CASE_METHOD(Fixture, "A rename counts as a change of the scene and comes back with undo", "[rename]")
{
    const core::NodeId grid = makeNode("helix::grid");
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setExpression(transform, "uniform_scale", 0, "prm(\"grid1.size\")").ok);
    const core::SceneSnapshot before = core::captureScene();
    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / ("helix_rename_" + std::to_string(::getpid()) + ".kut");
    REQUIRE(core::saveScene(file.string()).ok);
    std::filesystem::remove(file);
    CHECK_FALSE(core::isSceneModified());

    REQUIRE(core::renameNode(grid, "floor").ok);
    CHECK(core::isSceneModified());
    const std::uint64_t revision = core::sceneRevision();
    REQUIRE(core::renameNode(grid, "ground").ok);
    CHECK(core::sceneRevision() != revision);

    REQUIRE(core::restoreScene(before.data).ok);
    CHECK(namesOf() == std::vector<std::string>{"grid1", "transform1"});
}

TEST_CASE_METHOD(Fixture, "A free name is the wanted one, or the next number up", "[names]")
{
    CHECK(core::freeNodeName("grille1") == "grille1");
    makeNode("helix::grid", "grille1");
    CHECK(core::freeNodeName("grille1") == "grille2");
    makeNode("helix::grid", "grille2");
    makeNode("helix::grid", "grille10");
    CHECK(core::freeNodeName("grille1") == "grille3");
    CHECK(core::freeNodeName("grille10") == "grille11");
    CHECK(core::freeNodeName("onde") == "onde");
    // A number too large for an integer is not parsed.
    makeNode("helix::grid", "big99999999999999");
    CHECK(core::freeNodeName("big99999999999999") == "big1");
    // A name made this way is one createNode takes.
    CHECK(core::createNode("helix::grid", core::freeNodeName("grille1"), 0.f, 0.f).status.ok);
}

TEST_CASE_METHOD(Fixture, "A node made from an alias remembers it, and keeps it through a save", "[alias]")
{
    const core::NodeId mountain = makeNode("helix::mountain", "montagne1");
    const core::NodeId noise = makeNode("helix::attributeNoise");
    const core::NodeId cube = makeNode("helix::cube");

    CHECK(infoOf(mountain).type == "helix::attributeNoise");
    CHECK(infoOf(mountain).aliasType == "helix::mountain");
    CHECK(infoOf(noise).aliasType.empty());
    CHECK(infoOf(cube).aliasType.empty());

    const core::SceneSnapshot snapshot = core::captureScene();
    REQUIRE(snapshot.status.ok);
    REQUIRE(core::restoreScene(snapshot.data).ok);
    for (const core::NodeInfo& info : core::listNodes())
    {
        if (info.name == "montagne1") CHECK(info.aliasType == "helix::mountain");
        else CHECK(info.aliasType.empty());
    }
}

TEST_CASE_METHOD(Fixture, "A scene saved before nodes kept their alias still opens", "[alias]")
{
    makeNode("helix::mountain", "montagne1");
    makeNode("helix::cube");
    core::SceneSnapshot snapshot = core::captureScene();
    REQUIRE(snapshot.status.ok);

    // A node that has none writes no field, as a file written before this was kept does.
    const std::string field = "\"alias\"";
    std::size_t at = snapshot.data.find(field);
    REQUIRE(at != std::string::npos);
    std::size_t start = snapshot.data.rfind(',', at);
    std::size_t end = snapshot.data.find('"', snapshot.data.find(':', at) + 1);
    end = snapshot.data.find('"', end + 1);
    snapshot.data.erase(start, end + 1 - start);
    REQUIRE(snapshot.data.find(field) == std::string::npos);

    REQUIRE(core::restoreScene(snapshot.data).ok);
    REQUIRE(core::listNodes().size() == 2);
    for (const core::NodeInfo& info : core::listNodes()) CHECK(info.aliasType.empty());
}

TEST_CASE_METHOD(Fixture, "Nodes are copied and pasted with their links, values and keys", "[copy]")
{
    const core::NodeId grid = makeNode("helix::grid", "", 10.f, 20.f);
    const core::NodeId transform = makeNode("helix::transform", "", 30.f, 40.f);
    const core::NodeId outside = makeNode("helix::transform", "after", 50.f, 60.f);
    REQUIRE(core::connectNodes(grid, 0, transform, 0).ok);
    REQUIRE(core::setFloat(grid, "size", 0, 7.f).ok);
    REQUIRE(core::setInt(grid, "rows", 0, 5).ok);
    REQUIRE(core::setExpression(transform, "uniform_scale", 0, "prm(\"grid1.size\") * 0.5").ok);
    REQUIRE(core::setKeyframe(transform, "translate", 1, 1.f, 0.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframe(transform, "translate", 1, 24.f, 9.f, core::Interpolation::Linear).ok);
    // A link to a node that is not copied is not carried.
    REQUIRE(core::connectNodes(transform, 0, outside, 0).ok);

    const core::CopyNodesResult copy = core::copyNodes({transform, grid});
    REQUIRE(copy.status.ok);
    CHECK(copy.text.find("helix.nodes") != std::string::npos);

    const core::PasteNodesResult pasted = core::pasteNodes(copy.text, 100.f, 200.f);
    REQUIRE(pasted.status.ok);
    REQUIRE(pasted.nodes.size() == 2);
    // In the order they were made, whatever order they were asked in.
    CHECK(pasted.names == std::vector<std::string>{"grid2", "transform2"});
    const core::NodeId newGrid = pasted.nodes[0];
    const core::NodeId newTransform = pasted.nodes[1];

    CHECK(infoOf(newGrid).type == "helix::grid");
    CHECK(infoOf(newGrid).x == Approx(110.f));
    CHECK(infoOf(newGrid).y == Approx(220.f));
    CHECK(infoOf(newTransform).x == Approx(130.f));
    CHECK(core::getFloat(newGrid, "size", 0).value == Approx(7.f));
    CHECK(core::getInt(newGrid, "rows", 0).value == 5);

    // The expression reads the copy of the grid, not the grid.
    CHECK(expressionOf(newTransform, "uniform_scale") == "prm(\"grid2.size\") * 0.5");
    CHECK(expressionOf(transform, "uniform_scale") == "prm(\"grid1.size\") * 0.5");

    // The link between the copies, none to the cube, and the links of the originals intact.
    std::vector<core::LinkInfo> links = core::listLinks();
    CHECK(links.size() == 3);
    const auto linked = [&](core::NodeId from, core::NodeId to) {
        return std::any_of(links.begin(), links.end(), [&](const core::LinkInfo& link) {
            return link.sourceNode == from && link.targetNode == to;
        });
    };
    CHECK(linked(newGrid, newTransform));
    CHECK(linked(grid, transform));
    CHECK(linked(transform, outside));
    CHECK_FALSE(linked(newTransform, outside));

    // The keys come with the copy, and the original keeps its own.
    const std::vector<core::KeyframeTrackInfo> tracks = core::listKeyframes(newTransform);
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].parameter == "translate");
    CHECK(tracks[0].component == 1);
    CHECK(tracks[0].keys.size() == 2);
    CHECK(core::listKeyframes(transform).size() == 1);

    // The display node is not changed by a paste.
    CHECK(core::displayNode() == 0);
}

TEST_CASE_METHOD(Fixture, "A paste after a cut gives the nodes their names back", "[copy]")
{
    const core::NodeId grid = makeNode("helix::grid");
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setExpression(transform, "uniform_scale", 0, "prm(\"grid1.size\")").ok);
    REQUIRE(core::connectNodes(grid, 0, transform, 0).ok);

    const core::CopyNodesResult copy = core::copyNodes({grid, transform});
    REQUIRE(copy.status.ok);
    REQUIRE(core::deleteNode(grid).ok);
    REQUIRE(core::deleteNode(transform).ok);

    const core::PasteNodesResult pasted = core::pasteNodes(copy.text, 0.f, 0.f);
    REQUIRE(pasted.status.ok);
    CHECK(pasted.names == std::vector<std::string>{"grid1", "transform1"});
    CHECK(expressionOf(pasted.nodes[1], "uniform_scale") == "prm(\"grid1.size\")");
    CHECK(core::listLinks().size() == 1);
}

TEST_CASE_METHOD(Fixture, "A reference to a node that was not copied is kept", "[copy]")
{
    makeNode("helix::grid");
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setExpression(transform, "uniform_scale", 0, "prm(\"grid1.size\")").ok);

    const core::PasteNodesResult pasted = core::pasteNodes(core::copyNodes({transform}).text, 0.f, 0.f);
    REQUIRE(pasted.status.ok);
    CHECK(expressionOf(pasted.nodes[0], "uniform_scale") == "prm(\"grid1.size\")");
}

TEST_CASE_METHOD(Fixture, "A node made from an alias is pasted as the same alias", "[copy][alias]")
{
    const core::NodeId mountain = makeNode("helix::mountain", "montagne1");
    const core::NodeId noise = makeNode("helix::attributeNoise", "noise1");
    REQUIRE(core::setFloat(mountain, "gain", 0, 2.25f).ok);

    const core::PasteNodesResult pasted = core::pasteNodes(core::copyNodes({mountain, noise}).text, 0.f, 0.f);
    REQUIRE(pasted.status.ok);
    CHECK(pasted.names == std::vector<std::string>{"montagne2", "noise2"});
    CHECK(infoOf(pasted.nodes[0]).aliasType == "helix::mountain");
    CHECK(infoOf(pasted.nodes[0]).type == "helix::attributeNoise");
    CHECK(core::getFloat(pasted.nodes[0], "gain", 0).value == Approx(2.25f));
    CHECK(infoOf(pasted.nodes[1]).aliasType.empty());
}

TEST_CASE_METHOD(Fixture, "Several pastes of the same copy each get names of their own", "[copy]")
{
    const core::NodeId grid = makeNode("helix::grid");
    const std::string text = core::copyNodes({grid}).text;
    REQUIRE(core::pasteNodes(text, 0.f, 0.f).status.ok);
    REQUIRE(core::pasteNodes(text, 0.f, 0.f).status.ok);
    REQUIRE(core::pasteNodes(text, 0.f, 0.f).status.ok);
    CHECK(namesOf() == std::vector<std::string>{"grid1", "grid2", "grid3", "grid4"});
}

TEST_CASE_METHOD(Fixture, "Nodes that cannot be copied are refused", "[copy]")
{
    const core::NodeId grid = makeNode("helix::grid");
    CHECK_FALSE(core::copyNodes({}).status.ok);
    CHECK_FALSE(core::copyNodes({9999}).status.ok);
    CHECK_FALSE(core::copyNodes({grid, 9999}).status.ok);
    CHECK(core::copyNodes({grid, grid}).status.ok);
}

TEST_CASE_METHOD(Fixture, "Text that is not a copy is refused and nothing is made", "[copy]")
{
    const core::NodeId grid = makeNode("helix::grid");
    const std::string good = core::copyNodes({grid}).text;
    const std::size_t before = core::listNodes().size();

    const auto refused = [&](const std::string& text) {
        const core::PasteNodesResult result = core::pasteNodes(text, 0.f, 0.f);
        CHECK_FALSE(result.status.ok);
        CHECK(result.nodes.empty());
        CHECK(core::listNodes().size() == before);
        return result.status.error;
    };
    refused("");
    refused("hello");
    refused("{\"helixNodes\": 3}");
    refused("{\"other\": {}}");
    refused("[1, 2, 3]");
    CHECK(refused(edited(good, "helix\\.nodes", "other.format")) == "The text is not a copy of Helix nodes.");
    CHECK(refused(edited(good, "\"version\"\\s*:\\s*1", "\"version\": 99")).find("another version") != std::string::npos);
    refused(edited(good, "\"typeName\"\\s*:\\s*\"helix::grid\"", "\"typeName\": \"helix::nothing\""));
    refused(good.substr(0, good.size() / 2));
    CHECK_FALSE(core::pasteNodes(good, std::nanf(""), 0.f).status.ok);
    CHECK_FALSE(core::pasteNodes(good, 0.f, INFINITY).status.ok);
    CHECK(core::listNodes().size() == before);

    // The error does not repeat the text that was pasted.
    const std::string noise(100000, 'x');
    CHECK(refused(noise).size() < 200);
    CHECK(refused("{\"helixNodes\":{\"format\":\"" + noise + "\"}}").find("xxxx") == std::string::npos);
}

TEST_CASE_METHOD(Fixture, "A copy whose links make no sense is refused whole", "[copy]")
{
    const core::NodeId grid = makeNode("helix::grid");
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::connectNodes(grid, 0, transform, 0).ok);
    const std::string good = core::copyNodes({grid, transform}).text;
    const std::size_t before = core::listNodes().size();

    const auto replaced = [&](const std::string& key, const std::string& value, const std::string& with) {
        return edited(good, "\"" + key + "\"\\s*:\\s*" + value, "\"" + key + "\": " + with);
    };

    // A link to a node that is not there, to an output the node does not have, to an input it
    // does not have, from a node to itself, and a loop.
    CHECK_FALSE(core::pasteNodes(replaced("target", "1", "7"), 0.f, 0.f).status.ok);
    CHECK_FALSE(core::pasteNodes(replaced("sourceOutput", "0", "4"), 0.f, 0.f).status.ok);
    CHECK_FALSE(core::pasteNodes(replaced("targetInput", "0", "4"), 0.f, 0.f).status.ok);
    CHECK_FALSE(core::pasteNodes(replaced("target", "1", "0"), 0.f, 0.f).status.ok);
    CHECK_FALSE(core::pasteNodes(
        edited(good, "\"links\"\\s*:\\s*\\[", "\"links\": [{\"source\":1,\"sourceOutput\":0,\"target\":0,\"targetInput\":0},"),
        0.f, 0.f).status.ok);
    CHECK(core::listNodes().size() == before);
}

TEST_CASE_METHOD(Fixture, "A merge keeps every link into its multiple input when pasted", "[copy]")
{
    const core::NodeId first = makeNode("helix::grid");
    const core::NodeId second = makeNode("helix::cube");
    const core::NodeId merge = makeNode("helix::merge");
    REQUIRE(core::connectNodes(first, 0, merge, 0).ok);
    REQUIRE(core::connectNodes(second, 0, merge, 1).ok);

    const core::PasteNodesResult pasted = core::pasteNodes(core::copyNodes({first, second, merge}).text, 5.f, 5.f);
    REQUIRE(pasted.status.ok);
    CHECK(core::listLinks().size() == 4);
}

TEST_CASE_METHOD(Fixture, "A pasted copy is a scene that saves and opens", "[copy]")
{
    const core::NodeId grid = makeNode("helix::grid");
    REQUIRE(core::setKeyframe(grid, "size", 0, 1.f, 1.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframe(grid, "size", 0, 10.f, 4.f, core::Interpolation::Linear).ok);
    const core::PasteNodesResult pasted = core::pasteNodes(core::copyNodes({grid}).text, 0.f, 0.f);
    REQUIRE(pasted.status.ok);

    const core::SceneSnapshot snapshot = core::captureScene();
    REQUIRE(core::restoreScene(snapshot.data).ok);
    CHECK(core::listNodes().size() == 2);
    for (const core::NodeInfo& info : core::listNodes()) CHECK(core::listKeyframes(info.id).size() == 1);
}

TEST_CASE_METHOD(Fixture, "A rename reaches the expression kept under a keyframe", "[rename]")
{
    const core::NodeId grid = makeNode("helix::grid");
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setExpression(transform, "uniform_scale", 0, "prm(\"grid1.size\")").ok);
    // The first key moves the expression into the store of keyframes.
    REQUIRE(core::setKeyframe(transform, "uniform_scale", 0, 1.f, 2.f, core::Interpolation::Linear).ok);

    REQUIRE(core::renameNode(grid, "sol").ok);
    REQUIRE(core::removeKeyframe(transform, "uniform_scale", 0, 1.f).ok);

    CHECK(expressionOf(transform, "uniform_scale") == "prm(\"sol.size\")");
}

TEST_CASE_METHOD(Fixture, "A paste sends the expression kept under a keyframe to the copies", "[copy]")
{
    const core::NodeId grid = makeNode("helix::grid");
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::setExpression(transform, "uniform_scale", 0, "prm(\"grid1.size\")").ok);
    REQUIRE(core::setKeyframe(transform, "uniform_scale", 0, 1.f, 2.f, core::Interpolation::Linear).ok);

    const core::PasteNodesResult pasted = core::pasteNodes(core::copyNodes({grid, transform}).text, 0.f, 0.f);
    REQUIRE(pasted.status.ok);
    REQUIRE(pasted.names == std::vector<std::string>{"grid2", "transform2"});
    REQUIRE(core::removeKeyframe(pasted.nodes[1], "uniform_scale", 0, 1.f).ok);

    CHECK(expressionOf(pasted.nodes[1], "uniform_scale") == "prm(\"grid2.size\")");
    // The original keeps reading the original.
    REQUIRE(core::removeKeyframe(transform, "uniform_scale", 0, 1.f).ok);
    CHECK(expressionOf(transform, "uniform_scale") == "prm(\"grid1.size\")");
}

TEST_CASE_METHOD(Fixture, "A paste whose parameter cannot be restored leaves nothing behind", "[copy]")
{
    const core::NodeId grid = makeNode("helix::grid");
    const core::NodeId transform = makeNode("helix::transform");
    REQUIRE(core::connectNodes(grid, 0, transform, 0).ok);
    const std::string good = core::copyNodes({transform}).text;

    // An expression for a second component of a scalar parameter, in the block of uniform_scale.
    const std::string expressions =
        "(\"uniform_scale\"[\\s\\S]*?\"expressions\"\\s*:\\s*)\\[\\s*\\{\\s*\"nullopt\"\\s*:\\s*true\\s*\\}\\s*\\]";
    const std::string malformed = std::regex_replace(
        good, std::regex(expressions), "$1[{\"nullopt\": true}, {\"nullopt\": false, \"data\": \"1.0\"}]", std::regex_constants::format_first_only
    );
    REQUIRE(malformed != good);
    // The same, with the values of the parameter gone, so the check on the values has nothing to
    // compare with and only the rollback is left to catch the restore that throws.
    const std::string bare = std::regex_replace(
        malformed,
        std::regex("(\"uniform_scale\"\\s*,\\s*\"floatValues\"\\s*:\\s*)\\[[^\\]]*\\]"),
        "$1[]",
        std::regex_constants::format_first_only
    );
    REQUIRE(bare != malformed);

    const std::vector<std::string> namesBefore = namesOf();
    const std::size_t linksBefore = core::listLinks().size();
    const bool modifiedBefore = core::isSceneModified();

    // Refused by the check, with a message of its own, before any node is made.
    const core::PasteNodesResult refused = core::pasteNodes(malformed, 0.f, 0.f);
    CHECK_FALSE(refused.status.ok);
    CHECK(refused.status.error == "A parameter of the copy has an expression it cannot hold.");
    const core::PasteNodesResult result = core::pasteNodes(bare, 0.f, 0.f);
    CHECK_FALSE(result.status.ok);
    CHECK(result.nodes.empty());

    CHECK(namesOf() == namesBefore);
    CHECK(core::listLinks().size() == linksBefore);
    // The revision may grow while the node is made and taken away, the saved state does not.
    CHECK(core::isSceneModified() == modifiedBefore);
}
