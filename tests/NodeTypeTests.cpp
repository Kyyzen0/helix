#include "Engine/Network/NodeManifest.h"
#include "Engine/Network/NodeType.h"
#include <catch2/catch_test_macros.hpp>

using namespace helix;

TEST_CASE("A node type with only single ports has no multi input port")
{
    nt::NodeType nodeType;
    nodeType.inputPorts = {{"Backbone", false}, {"Profile", false}};

    REQUIRE(!nodeType.hasMultiInputPort());
    REQUIRE(nodeType.getSinglePortCount() == 2);
}

TEST_CASE("The single ports are the ones below a multi input port")
{
    nt::NodeType nodeType;
    nodeType.inputPorts = {{"Geometry", false}, {"Cutters", true}};

    REQUIRE(nodeType.hasMultiInputPort());
    REQUIRE(nodeType.getSinglePortCount() == 1);
}

TEST_CASE("Every index at or above a multi input port belongs to it")
{
    nt::NodeType nodeType;
    nodeType.inputPorts = {{"Geometry", false}, {"Cutters", true}};

    REQUIRE(nodeType.getPortAt(0)->label == "Geometry");
    REQUIRE(nodeType.getPortAt(1)->label == "Cutters");
    REQUIRE(nodeType.getPortAt(7)->label == "Cutters");
}

TEST_CASE("An index past the last single input belongs to nothing")
{
    nt::NodeType nodeType;
    nodeType.inputPorts = {{"Geometry", false}};

    REQUIRE(nodeType.getPortAt(1) == nullptr);
}

TEST_CASE("A node type declaring no inputs addresses nothing")
{
    nt::NodeType nodeType;

    REQUIRE(!nodeType.hasMultiInputPort());
    REQUIRE(nodeType.getSinglePortCount() == 0);
    REQUIRE(nodeType.getPortAt(0) == nullptr);
}

// User authored nodes

TEST_CASE("A user node's type carries the ports its manifest names")
{
    const nt::NodeManifest manifest = nt::NodeManifest::loadFromString(R"(
version: 1
name: myWiggle
namespace: user
label: My Wiggle
tags: [user]
inputs:
  - label: Geometry
outputs: 1
implementation:
  kind: script
  source: source.das
)");
    const nt::NodeType& nodeType = manifest.getNodeType();

    REQUIRE(nodeType.getFullName() == "user::myWiggle");
    REQUIRE(nodeType.getLabel() == "My Wiggle");
    REQUIRE(nodeType.getSinglePortCount() == 1);
    REQUIRE(nodeType.getPortAt(0)->label == "Geometry");
}
