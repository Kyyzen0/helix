#include "Engine/Attribute/AttributeHandle.h"
#include "Engine/Network/NodeAlias.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeLoader.h"
#include "Engine/Network/NodeType.h"
#include "Engine/Network/NodeTypeTable.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Serializer/Serializer.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace enzo;

namespace {

// Returns every folder that claims to be a node, read straight off disk rather
// than from the table, so a node the loader dropped still shows up here.
std::vector<std::string> shippedNodeNames()
{
    std::vector<std::string> names;
    for (const auto& entry :
         std::filesystem::directory_iterator(nt::NodeLoader::getNodesDirectory()))
        if (std::filesystem::exists(entry.path() / "node.yaml"))
            names.push_back(entry.path().filename().string());
    return names;
}

} // namespace

TEST_CASE("Every shipped node loads and resolves its implementation")
{
    nt::NodeLoader::loadNodes();

    // A node is declared in yaml rather than C++, so a broken one only shows up
    // at runtime. This is the check that catches it.
    for (const std::string& name : shippedNodeNames())
    {
        INFO("node " << name);

        // Every shipped node is published under the enzo namespace.
        const std::string fullName = "enzo::" + name;
        if (const nt::NodeAlias* nodeAlias = nt::NodeTypeTable::getNodeAlias(fullName))
        {
            REQUIRE(nt::NodeTypeTable::getNodeType(nodeAlias->aliasedType) != nullptr);
            continue;
        }

        const nt::NodeType* nodeType = nt::NodeTypeTable::getNodeType(fullName);
        REQUIRE(nodeType != nullptr);
        REQUIRE(nodeType->ctorFunc != nullptr);
        REQUIRE(nodeType->folder.filename() == name);
    }
}

TEST_CASE("Loading twice leaves one entry per node")
{
    nt::NodeLoader::loadNodes();
    const size_t loadedCount = nt::NodeTypeTable::getData().size();

    nt::NodeLoader::loadNodes();

    REQUIRE(nt::NodeTypeTable::getData().size() == loadedCount);

    // Other tests register their own node types in the shared table, so a shipped node is
    // checked for a single entry by name
    for (const std::string& name : shippedNodeNames())
    {
        INFO("node " << name);

        const std::string fullName = "enzo::" + name;
        size_t entryCount = 0;
        for (const nt::NodeType& nodeType : nt::NodeTypeTable::getData())
            if (nodeType.getFullName() == fullName) entryCount++;
        for (const nt::NodeAlias& nodeAlias : nt::NodeTypeTable::getNodeAliases())
            if (nodeAlias.getFullName() == fullName) entryCount++;

        REQUIRE(entryCount == 1);
    }
}

// User authored nodes

namespace {

// Writes a folder holding a user node's manifest and the source it names.
std::filesystem::path writeUserNodeFolder(
    const std::filesystem::path& root,
    const std::string& folderName,
    const std::string& manifest,
    const std::string& source
)
{
    const std::filesystem::path folder = root / folderName;
    std::filesystem::create_directories(folder);
    {
        std::ofstream manifestFile(folder / "node.yaml");
        manifestFile << manifest;
    }
    {
        std::ofstream sourceFile(folder / "source.das");
        sourceFile << source;
    }
    return folder;
}

// The manifest of a user node. The source's file name is the only thing the
// implementation needs, so the library and the constructor keep their
// defaults.
//
// The name in the manifest is the name the node type is known by, so the
// manifest is built for the node the test writes. A manifest that carries
// another name would register that other name.
std::string userNodeManifest(const std::string& name)
{
    return R"(version: 1

name: )" + name + R"(
namespace: user
label: My Node
tags: [user]

inputs:
  - label: Geometry
outputs: 1

implementation:
  kind: script
  source: source.das
)";
}

// Returns a folder no other test uses, so the user nodes of one test do not
// reach the node type table of another.
std::filesystem::path userNodeRoot(const std::string& name)
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("enzo_user_node_" + name);
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

} // namespace

TEST_CASE("A user node registers its type and reaches the node type table")
{
    const std::filesystem::path folder = writeUserNodeFolder(
        userNodeRoot("registered"), "myWiggle", userNodeManifest("myWiggle"), "@height = 1.0"
    );

    std::string error;
    REQUIRE(nt::NodeLoader::registerUserNode(folder, error));
    REQUIRE(error.empty());

    const nt::NodeType* nodeType = nt::NodeTypeTable::getNodeType("user::myWiggle");
    REQUIRE(nodeType != nullptr);
    REQUIRE(nodeType->getLabel() == "My Node");
    REQUIRE(nodeType->inputPorts.size() == 1);
    REQUIRE(nodeType->inputPorts.at(0).label == "Geometry");
    REQUIRE(nt::NodeLoader::getUserScript(folder) != nullptr);
}

TEST_CASE("A user node whose source does not compile is left out with an error")
{
    const std::filesystem::path folder = writeUserNodeFolder(
        userNodeRoot("broken_source"), "myBrokenNode", userNodeManifest("myBrokenNode"), "@height = "
    );

    std::string error;
    REQUIRE_FALSE(nt::NodeLoader::registerUserNode(folder, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(nt::NodeTypeTable::getNodeType("user::myBrokenNode") == nullptr);
    REQUIRE(nt::NodeLoader::getUserScript(folder) == nullptr);
}

TEST_CASE("A user node with a broken manifest is left out with an error")
{
    const std::filesystem::path folder = writeUserNodeFolder(
        userNodeRoot("broken_manifest"),
        "myBrokenManifest",
        "version: 1\nname: myBrokenManifest\nnamespace: user\n",
        "@height = 1.0"
    );

    std::string error;
    REQUIRE_FALSE(nt::NodeLoader::registerUserNode(folder, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(nt::NodeTypeTable::getNodeType("user::myBrokenManifest") == nullptr);
}

TEST_CASE("A user node whose source is missing is left out with an error")
{
    const std::filesystem::path folder = writeUserNodeFolder(
        userNodeRoot("missing_source"), "myMissingSource", userNodeManifest("myMissingSource"), "@height = 1.0"
    );
    std::filesystem::remove(folder / "source.das");

    std::string error;
    REQUIRE_FALSE(nt::NodeLoader::registerUserNode(folder, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(nt::NodeTypeTable::getNodeType("user::myMissingSource") == nullptr);
}

TEST_CASE("A user node runs its source over the geometry arriving at its input")
{
    const std::filesystem::path folder = writeUserNodeFolder(
        userNodeRoot("cooked"), "myCookedNode", userNodeManifest("myCookedNode"), "@height = 3.0"
    );

    std::string error;
    REQUIRE(nt::NodeLoader::registerUserNode(folder, error));

    nt::nm()._reset();
    auto& nm = nt::nm();
    const nt::NodeId grid = nm.createNode("enzo::grid");
    const nt::NodeId user = nm.createNode("user::myCookedNode");
    nm.connectNodes(grid, 0, user, 0);
    nm.cook(user);

    const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(
        nm.getNode(user).getOutputPacket(0)->getPrimitive(0)
    );
    REQUIRE(mesh != nullptr);

    const auto height = mesh->getAttribByName(attr::AttributeOwner::POINT, "height", true);
    REQUIRE(height != nullptr);
    const attr::AttributeHandleRO<floatT> heightValues(height);
    REQUIRE(heightValues.getValue(0) == Catch::Approx(3.0f));
}

TEST_CASE("A user node survives a save and a reload of the project")
{
    const std::filesystem::path folder = writeUserNodeFolder(
        userNodeRoot("saved"), "mySavedNode", userNodeManifest("mySavedNode"), "@height = 2.0"
    );

    std::string error;
    REQUIRE(nt::NodeLoader::registerUserNode(folder, error));

    nt::nm()._reset();
    auto& nm = nt::nm();
    const nt::NodeId grid = nm.createNode("enzo::grid");
    const nt::NodeId user = nm.createNode("user::mySavedNode");
    nm.connectNodes(grid, 0, user, 0);
    nm.cook(user);

    const std::string path = "/tmp/enzo_user_node_roundtrip.json";
    nt::Serializer serializer;
    serializer.save(nm, path);

    nm._reset();
    serializer.load(nm, path);

    // The scene comes back with its nodes and its links. What a snapshot
    // holds is the parameters of a node and not the result of its cook, so
    // the geometry is cooked again.
    REQUIRE(nm.nodes().size() == 2);
    nt::NodeId reloadedUser = nt::nullNode;
    bool foundUserNode = false;
    for (auto [nodeId, node] : nm.nodes())
    {
        if (node.getType().getFullName() != "user::mySavedNode") continue;
        foundUserNode = true;
        reloadedUser = nodeId;
    }
    REQUIRE(foundUserNode);
    REQUIRE(reloadedUser != nt::nullNode);
    nm.cook(reloadedUser);

    // The user node comes back with its type, its link, and its source.
    for (auto [nodeId, node] : nm.nodes())
    {
        if (node.getType().getFullName() != "user::mySavedNode") continue;

        const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(
            node.getOutputPacket(0)->getPrimitive(0)
        );
        REQUIRE(mesh != nullptr);
        const auto height = mesh->getAttribByName(attr::AttributeOwner::POINT, "height", true);
        REQUIRE(height != nullptr);
        REQUIRE(attr::AttributeHandleRO<floatT>(height).getValue(0) == Catch::Approx(2.0f));
    }
}

// The sweep node

TEST_CASE("The sweep node sweeps a profile along a curve")
{
    nt::nm()._reset();
    nt::NodeLoader::loadNodes();

    auto& nm = nt::nm();
    const nt::NodeId line = nm.createNode("enzo::line");
    const nt::NodeId sweep = nm.createNode("enzo::sweep");
    nm.connectNodes(line, 0, sweep, 0);
    nm.cook(sweep);

    const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(
        nm.getNode(sweep).getOutputPacket(0)->getPrimitive(0)
    );
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->getNumFaces() > 0);

    // The line runs along the Y axis, so every face of the tube around it turns
    // away from that axis.
    const geo::FaceNormalHandle normals = mesh->getFaceNormal();
    for (Offset face = 0; face < mesh->getNumFaces(); ++face)
    {
        const Vector3 normal = normals[face];
        REQUIRE(normal.norm() > 0.5f);

        Vector3 centre = Vector3::Zero();
        int pointCount = 0;
        for (intT point : mesh->getFacePoints(face))
        {
            centre += mesh->getPointPos(point);
            ++pointCount;
        }
        centre /= static_cast<float>(pointCount);

        const Vector3 outward = Vector3(centre.x(), 0.0f, centre.z()).normalized();
        REQUIRE(normal.dot(outward) > 0.0f);
    }
}
