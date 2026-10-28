#include "Engine/Attribute/AttributeHandle.h"
#include "Engine/Core/Types.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeLoader.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Parameter/Ramp.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Serializer/Serializer.h"
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct NMReset
{
    NMReset()
    {
        helix::nt::NodeLoader::loadNodes();
        helix::nt::nm()._reset();
    }
    ~NMReset() { helix::nt::nm()._reset(); }
};

TEST_CASE_METHOD(NMReset, "Network Manager")
{
    using namespace helix;

    auto& nm = nt::nm();

    nt::NodeId startNode = nm.createNode("helix::cube");
    nt::NodeId prevNode = startNode;
    std::vector<nt::NodeId> prevNodes;

    for (int k = 0; k < 10; k++)
    {
        for (int i = 0; i < 4; ++i)
        {
            nt::NodeId newNode = nm.createNode("helix::cube");
            prevNodes.push_back(newNode);
            nt::nm().connectNodes(newNode, i, prevNode, 0);
        }
        for (int j = 0; j < 10; j++)
        {
            std::vector<nt::NodeId> prevNodesBuffer = prevNodes;
            for (int i = 0; i < size(prevNodesBuffer); ++i)
            {
                prevNodes.clear();
                nt::NodeId newNode = nm.createNode("helix::cube");
                prevNodes.push_back(newNode);
                nt::nm().connectNodes(newNode, 0, prevNodesBuffer[i], 0);
            }
        }
    }

    BENCHMARK("Cook 100 Nodes") { nm.setDisplayNode(startNode); };
}

namespace {

// Sets a sphere node's radius and division counts.
void setSphereShape(helix::nt::Node& sphere, float radius, int columns, int rows)
{
    for (int axis = 0; axis < 3; ++axis)
        sphere.getParameter("radius").lock()->setFloat(radius, axis);
    sphere.getParameter("columns").lock()->setInt(columns);
    sphere.getParameter("rows").lock()->setInt(rows);
}

// Returns a cooked fracture of a dense sphere around seed points on three nested spheres.
helix::nt::NodeId addFractureOfSphere(int seedColumns, int seedRows)
{
    using namespace helix;
    auto& nm = nt::nm();

    const nt::NodeId geometry = nm.createNode("helix::sphere");
    setSphereShape(nm.getNode(geometry), 1.0f, 64, 32);

    // Turns each seed sphere by a different angle so their rings do not line up.
    const nt::NodeId seeds = nm.createNode("helix::merge");
    const float seedRadii[] = {0.3f, 0.6f, 0.9f};
    for (int shellIndex = 0; shellIndex < 3; ++shellIndex)
    {
        const nt::NodeId shell = nm.createNode("helix::sphere");
        nt::Node& shellNode = nm.getNode(shell);
        setSphereShape(shellNode, seedRadii[shellIndex], seedColumns, seedRows);
        shellNode.getParameter("rotate").lock()->setFloat(17.0f * (shellIndex + 1), 0);
        shellNode.getParameter("rotate").lock()->setFloat(29.0f * (shellIndex + 1), 1);
        nm.connectNodes(shell, 0, seeds, 0);
    }

    const nt::NodeId fracture = nm.createNode("helix::cellFracture");
    nm.connectNodes(geometry, 0, fracture, 0);
    nm.connectNodes(seeds, 0, fracture, 1);
    nm.cook(fracture);
    return fracture;
}

// Prints the point, face, piece and inside face counts of a fracture's output.
void printFractureShape(helix::nt::NodeId fracture)
{
    using namespace helix;
    auto& nm = nt::nm();
    const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(
        nm.getNode(fracture).getOutputPacket(0)->getPrimitive(0)
    );
    REQUIRE(mesh != nullptr);

    attr::AttributeHandleRO<intT> piece(mesh->getAttribByName(attr::AttributeOwner::FACE, "piece"));
    attr::AttributeHandleRO<boolT> inside(
        mesh->getGroupByName(attr::AttributeOwner::FACE, "inside")
    );
    std::set<intT> pieceNumbers;
    int insideFaceCount = 0;
    for (Offset faceOffset = 0; faceOffset < mesh->getNumFaces(); ++faceOffset)
    {
        pieceNumbers.insert(piece.getValue(faceOffset));
        if (inside.getValue(faceOffset)) ++insideFaceCount;
    }

    std::cout << "fracture shape: points " << mesh->getNumPoints() << ", faces "
              << mesh->getNumFaces() << ", pieces " << pieceNumbers.size() << ", inside faces "
              << insideFaceCount << "\n";
}

} // namespace

TEST_CASE_METHOD(NMReset, "Cell fracture")
{
    using namespace helix;
    auto& nm = nt::nm();

    // Cooks only the fracture each run, since the nodes above it stay clean.
    const nt::NodeId fewSeeds = addFractureOfSphere(4, 3);
    printFractureShape(fewSeeds);
    BENCHMARK("Fracture sphere around 30 seeds")
    {
        nm.getNode(fewSeeds).dirtyNode(false);
        nm.cook(fewSeeds);
    };

    const nt::NodeId manySeeds = addFractureOfSphere(8, 5);
    printFractureShape(manySeeds);
    BENCHMARK("Fracture sphere around 102 seeds")
    {
        nm.getNode(manySeeds).dirtyNode(false);
        nm.cook(manySeeds);
    };

    const nt::NodeId denseSeeds = addFractureOfSphere(25, 13);
    printFractureShape(denseSeeds);
    BENCHMARK("Fracture sphere around 906 seeds")
    {
        nm.getNode(denseSeeds).dirtyNode(false);
        nm.cook(denseSeeds);
    };
}

TEST_CASE_METHOD(NMReset, "Cell fracture of a cube")
{
    using namespace helix;
    auto& nm = nt::nm();

    // Places every seed on a sphere around the cube, so every halfway plane passes through its
    // center.
    const nt::NodeId cube = nm.createNode("helix::cube");
    const nt::NodeId seeds = nm.createNode("helix::sphere");
    setSphereShape(nm.getNode(seeds), 1.0f, 30, 30);
    const nt::NodeId fracture = nm.createNode("helix::cellFracture");
    nm.connectNodes(cube, 0, fracture, 0);
    nm.connectNodes(seeds, 0, fracture, 1);
    nm.cook(fracture);
    printFractureShape(fracture);

    BENCHMARK("Fracture cube around 872 seeds on a sphere")
    {
        nm.getNode(fracture).dirtyNode(false);
        nm.cook(fracture);
    };
}

namespace {

// Returns a grid node with the given division counts.
helix::nt::NodeId addGrid(int rows, int columns)
{
    using namespace helix;
    auto& nm = nt::nm();

    const nt::NodeId grid = nm.createNode("helix::grid");
    nm.getNode(grid).getParameter("rows").lock()->setInt(rows);
    nm.getNode(grid).getParameter("columns").lock()->setInt(columns);
    return grid;
}

// Returns a cooked copy to points node wiring the prototype onto the target points.
helix::nt::NodeId addCopyToPoints(helix::nt::NodeId prototype, helix::nt::NodeId targetPoints)
{
    using namespace helix;
    auto& nm = nt::nm();

    const nt::NodeId copy = nm.createNode("helix::copyToPoints");
    nm.connectNodes(prototype, 0, copy, 0);
    nm.connectNodes(targetPoints, 0, copy, 1);
    nm.cook(copy);
    return copy;
}

// Prints the point and face counts of a node's output mesh.
void printMeshShape(const std::string& label, helix::nt::NodeId node)
{
    using namespace helix;
    const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(
        nt::nm().getNode(node).getOutputPacket(0)->getPrimitive(0)
    );
    REQUIRE(mesh != nullptr);
    std::cout << label << ": points " << mesh->getNumPoints() << ", faces " << mesh->getNumFaces()
              << "\n";
}

} // namespace

TEST_CASE_METHOD(NMReset, "Copy dense prototype to points")
{
    using namespace helix;
    auto& nm = nt::nm();

    const nt::NodeId denseSphere = nm.createNode("helix::sphere");
    setSphereShape(nm.getNode(denseSphere), 1.0f, 400, 250);
    const nt::NodeId fewPoints = addGrid(4, 4);
    const nt::NodeId denseOntoFew = addCopyToPoints(denseSphere, fewPoints);
    printMeshShape("dense prototype", denseSphere);
    printMeshShape("few target points", fewPoints);
    printMeshShape("dense onto few", denseOntoFew);
    // Cooks only the copy each run, since the nodes above it stay clean.
    BENCHMARK("Copy dense sphere onto few points")
    {
        nm.getNode(denseOntoFew).dirtyNode(false);
        nm.cook(denseOntoFew);
    };
}

TEST_CASE_METHOD(NMReset, "Copy to many points")
{
    using namespace helix;
    auto& nm = nt::nm();

    const nt::NodeId cube = nm.createNode("helix::cube");
    const nt::NodeId manyPoints = addGrid(316, 316);
    const nt::NodeId cubeOntoMany = addCopyToPoints(cube, manyPoints);
    printMeshShape("many target points", manyPoints);
    printMeshShape("cube onto many", cubeOntoMany);
    BENCHMARK("Copy cube onto many points")
    {
        nm.getNode(cubeOntoMany).dirtyNode(false);
        nm.cook(cubeOntoMany);
    };
}

TEST_CASE_METHOD(NMReset, "Copy medium prototype to medium points")
{
    using namespace helix;
    auto& nm = nt::nm();

    const nt::NodeId mediumSphere = nm.createNode("helix::sphere");
    setSphereShape(nm.getNode(mediumSphere), 1.0f, 32, 32);
    const nt::NodeId mediumPoints = addGrid(32, 32);
    const nt::NodeId mediumOntoMedium = addCopyToPoints(mediumSphere, mediumPoints);
    printMeshShape("medium prototype", mediumSphere);
    printMeshShape("medium target points", mediumPoints);
    printMeshShape("medium onto medium", mediumOntoMedium);
    BENCHMARK("Copy medium sphere onto medium points")
    {
        nm.getNode(mediumOntoMedium).dirtyNode(false);
        nm.cook(mediumOntoMedium);
    };
}

TEST_CASE("Ramp sampling")
{
    using namespace helix;

    // A curved run bordered by linear keys, the shape the per point hotpath sees.
    prm::Ramp ramp(
        std::vector<prm::Ramp::Key>{
            {0.0f, 0.0f, prm::Interpolation::LINEAR},
            {0.2f, 1.0f, prm::Interpolation::BSPLINE},
            {0.4f, 0.0f, prm::Interpolation::BSPLINE},
            {0.6f, 2.0f, prm::Interpolation::BSPLINE},
            {0.8f, 1.0f, prm::Interpolation::LINEAR},
            {1.0f, 0.0f, prm::Interpolation::LINEAR},
        }
    );

    BENCHMARK("Sample b spline ramp 10k points")
    {
        floatT total = 0;
        for (int i = 0; i < 10000; ++i)
            total += ramp.sample(i / 10000.0f);
        return total;
    };
}

TEST_CASE_METHOD(NMReset, "Script attribute reads")
{
    using namespace helix;
    auto& nm = nt::nm();

    const nt::NodeId grid = addGrid(1000, 1000);
    const nt::NodeId script = nm.createNode("helix::script");
    nm.connectNodes(grid, 0, script, 0);
    nm.cook(script);
    printMeshShape("script input", grid);

    // Cooks only the script each run, since the grid above it stays clean.
    const auto benchmarkCode = [&](const std::string& name, const std::string& code) {
        nm.getNode(script).getParameter("code").lock()->setString(code);
        BENCHMARK(name.c_str())
        {
            nm.getNode(script).dirtyNode(false);
            nm.cook(script);
        };
    };

    benchmarkCode("Write a constant", "@height = 1.0");
    benchmarkCode("Move every position", "v@Position += 0.1");
    benchmarkCode("Read every point", R"(v@copy = pointAttrVector("Position", curPt()))");
    benchmarkCode("Read the wrong type on every point", R"(@copy = pointAttr("Position", curPt()))");
    benchmarkCode("Read a missing attribute on every point", R"(@copy = pointAttr("missing", curPt()))");
}

namespace {

// Returns the x coordinate of the first point a cooked node produced.
float firstPointX(helix::nt::NodeId node)
{
    using namespace helix;
    const auto packet = nt::nm().getNode(node).getOutputPacket(0);
    REQUIRE(packet != nullptr);
    const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(packet->getPrimitive(0));
    REQUIRE(mesh != nullptr);
    return mesh->getPointPos(0).x();
}

// Wires a grid and a chain of transform nodes in series, and makes the last one the display node.
// Returns the transforms in order, the first one being right below the grid.
std::vector<helix::nt::NodeId> addTransformChain(int length, int gridSide)
{
    using namespace helix;
    auto& nm = nt::nm();

    nt::NodeId previous = addGrid(gridSide, gridSide);
    std::vector<nt::NodeId> chain;
    for (int i = 0; i < length; ++i)
    {
        const nt::NodeId transform = nm.createNode("helix::transform");
        nm.connectNodes(previous, 0, transform, 0);
        chain.push_back(transform);
        previous = transform;
    }
    nm.setDisplayNode(chain.back());
    return chain;
}

} // namespace

// The latency of a parameter change is what docs/PERFORMANCE.md targets. Each run changes a value
// that no earlier run used, so the change really dirties the graph, and then cooks the display
// node on the calling thread.
TEST_CASE_METHOD(NMReset, "Parameter change latency in a chain")
{
    using namespace helix;
    auto& nm = nt::nm();

    const std::vector<nt::NodeId> chain = addTransformChain(100, 100);
    const nt::NodeId display = chain.back();
    nm.cook(display);

    // Outside the measure: the change must reach the end of the chain.
    const auto translateOfFirst = nm.getNode(chain.front()).getParameter("translate").lock();
    const float before = firstPointX(display);
    translateOfFirst->setFloat(3.0f, 0);
    nm.cook(display);
    REQUIRE(firstPointX(display) != before);

    float value = 10.0f;
    BENCHMARK("Change a float parameter in a 100 node chain and cook")
    {
        value += 1.0f;
        translateOfFirst->setFloat(value, 0);
        nm.cook(display);
    };
}

// Each of the 100 transforms reads a driver node through an expression, so a change of the driver
// goes through the evaluation of 100 expressions before the cook.
TEST_CASE_METHOD(NMReset, "Parameter change latency through expressions")
{
    using namespace helix;
    auto& nm = nt::nm();

    // The first transform made is named transform1, which the expressions read.
    const nt::NodeId driver = nm.createNode("helix::transform");
    const std::vector<nt::NodeId> chain = addTransformChain(100, 100);
    const nt::NodeId display = chain.back();
    for (nt::NodeId reader : chain)
        nm.getNode(reader).getParameter("translate").lock()->setExpression(
            "prm(\"transform1.translate\") + 0.01"
        );
    nm.cook(display);

    // Outside the measure: the change of the driver must reach the end of the chain.
    const auto translateOfDriver = nm.getNode(driver).getParameter("translate").lock();
    const float before = firstPointX(display);
    translateOfDriver->setFloat(3.0f, 0);
    nm.cook(display);
    REQUIRE(firstPointX(display) != before);

    float value = 10.0f;
    BENCHMARK("Change a float parameter read by 100 expressions and cook")
    {
        value += 1.0f;
        translateOfDriver->setFloat(value, 0);
        nm.cook(display);
    };
}

// Loading clears the network first, so each run starts from the same state.
TEST_CASE_METHOD(NMReset, "Scene load")
{
    using namespace helix;
    auto& nm = nt::nm();

    addTransformChain(99, 10);
    const std::string path = "/tmp/helix_bench_scene_100_nodes.json";
    nt::Serializer serializer;
    serializer.save(nm, path);

    serializer.load(nm, path);
    REQUIRE(nm.nodes().size() == 100);

    BENCHMARK("Load a scene of 100 nodes") { serializer.load(nm, path); };
}

// The performance measurements live in their own file, because the bench target
// compiles this file alone and CMakeLists.txt belongs to the build owner.
#include "PerfMeasurements.inc"
