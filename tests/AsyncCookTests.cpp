#include "Engine/Async/CookResult.h"
#include "Engine/Async/CookTypes.h"
#include "Engine/Core/Types.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeImpl.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Network/NodeType.h"
#include "Engine/Network/NodeTypeTable.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Parameter/Template.h"
#include "Engine/Primitives/Mesh.h"
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace helix;

namespace {

/**
 * @brief What the probe nodes notice while they cook.
 *
 * The probe nodes are the test's window into the cook: they count how many of
 * them ran at the same time, and how often they ran at all.
 */
struct ProbeState
{
    mutable std::mutex mutex;
    std::condition_variable cv;

    // How many probes are cooking right now.
    int running = 0;
    // The most that were ever cooking at the same time.
    int maxRunning = 0;
    // How many probes have cooked in total.
    int cooked = 0;
    // How long each probe holds its cook open.
    std::atomic<int> holdMs{0};

    void enter()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++running;
        ++cooked;
        if (running > maxRunning) maxRunning = running;
        cv.notify_all();
    }

    void leave()
    {
        std::lock_guard<std::mutex> lock(mutex);
        --running;
        cv.notify_all();
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(mutex);
        running = 0;
        maxRunning = 0;
        cooked = 0;
        holdMs = 0;
    }

    int maxConcurrent() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return maxRunning;
    }

    int cookCount() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return cooked;
    }
};

ProbeState& probeState()
{
    static ProbeState state;
    return state;
}

// A node that notes that it ran, and holds its cook open long enough for
// another cook to overlap it.
class Probe : public nt::NodeImpl
{
  public:
    using NodeImpl::NodeImpl;

    void cook() override
    {
        ProbeState& state = probeState();
        state.enter();
        std::this_thread::sleep_for(std::chrono::milliseconds(state.holdMs));
        state.leave();

        // The geometry records how many points the node was asked for, so a
        // test can tell which cook produced it.
        const intT points = evalParmInt("points");

        NodePacket packet;
        auto mesh = std::make_shared<geo::Mesh>("/probe");
        std::vector<Vector3> positions;
        for (intT point = 0; point < points; ++point)
            positions.push_back(Vector3(static_cast<floatT>(point), 0, 0));
        mesh->addPoints(positions);
        packet.addPrimitive(mesh);
        setOutputPacket(0, packet);
    }
};

// A node that passes the geometry of its inputs through and grows it by a
// number of points, so the result says which cook produced it.
class Grow : public nt::NodeImpl
{
  public:
    using NodeImpl::NodeImpl;

    void cook() override
    {
        const intT grow = evalParmInt("grow");

        std::vector<Vector3> positions;
        const unsigned int inputCount = getInputCount();
        for (unsigned int inputIndex = 0; inputIndex < inputCount; ++inputIndex)
        {
            if (!hasInput(inputIndex)) continue;
            const std::shared_ptr<const NodePacket> input = getInputPacket(inputIndex);
            for (const std::shared_ptr<geo::Primitive>& primitive : input->getPrimitives())
            {
                auto mesh = std::dynamic_pointer_cast<geo::Mesh>(primitive);
                if (!mesh) continue;
                for (const Vector3& position : mesh->pointPosSpan()) positions.push_back(position);
            }
        }

        for (intT point = 0; point < grow; ++point)
            positions.push_back(Vector3(static_cast<floatT>(1000 + point), 0, 0));

        NodePacket packet;
        auto mesh = std::make_shared<geo::Mesh>("/grow");
        mesh->addPoints(positions);
        packet.addPrimitive(mesh);
        setOutputPacket(0, packet);
    }
};

nt::NodeImpl* makeProbe(nt::Node& node, nt::CookContext& context)
{
    return new Probe(node, context);
}

nt::NodeImpl* makeGrow(nt::Node& node, nt::CookContext& context)
{
    return new Grow(node, context);
}

nt::NodeType makeType(
    std::string name,
    std::string label,
    std::vector<prm::Template> templates,
    std::vector<nt::InputPort> inputPorts,
    nt::nodeConstructor constructor
)
{
    nt::NodeType type;
    type.internalName = std::move(name);
    type.typeNamespace = "test";
    type.displayName = std::move(label);
    type.ctorFunc = constructor;
    type.templates = std::move(templates);
    type.inputPorts = std::move(inputPorts);
    type.maxOutputs = 1;
    return type;
}

/// @brief The name of the probe node type, registered the first time it is asked for.
/// @note Registering on demand rather than at startup keeps the type table from
/// being filled before the table itself exists.
const std::string& probeTypeName()
{
    static const std::string name = nt::NodeTypeTable::addNodeType(makeType(
        "probe",
        "Probe",
        {prm::Template(prm::Type::INT, prm::Name("points"), prm::Default(3))},
        {},
        &makeProbe
    )).getFullName();
    return name;
}

/// @brief The name of the grow node type, registered the first time it is asked for.
const std::string& growTypeName()
{
    static const std::string name = nt::NodeTypeTable::addNodeType(makeType(
        "grow",
        "Grow",
        {prm::Template(prm::Type::INT, prm::Name("grow"), prm::Default(1))},
        {nt::InputPort{"in", true, false}},
        &makeGrow
    )).getFullName();
    return name;
}

/// @brief Puts the network back to a bare state between the tests.
struct NetworkReset
{
    NetworkReset() { nt::nm()._reset(); }
    ~NetworkReset() { nt::nm()._reset(); }
};

/// @brief Sets a parameter and returns the node it belongs to.
void setIntParameter(nt::NodeId nodeId, const std::string& name, int value)
{
    std::weak_ptr<prm::NodeParameter> parameter = nt::nm().getNode(nodeId).getParameter(name);
    REQUIRE(parameter.lock());
    parameter.lock()->setInt(value);
}

/// @brief The shape of a geometry, so two cooks can be compared.
struct GeometryShape
{
    std::vector<size_t> pointCounts;
    std::vector<std::vector<Vector3>> positions;
};

bool operator==(const GeometryShape& left, const GeometryShape& right)
{
    return left.pointCounts == right.pointCounts && left.positions == right.positions;
}

GeometryShape shapeOf(const NodePacket& packet)
{
    GeometryShape shape;
    for (const std::shared_ptr<geo::Primitive>& primitive : packet.getPrimitives())
    {
        auto mesh = std::dynamic_pointer_cast<geo::Mesh>(primitive);
        if (!mesh) continue;
        shape.pointCounts.push_back(mesh->pointPosSpan().size());
        shape.positions.emplace_back(mesh->pointPosSpan().begin(), mesh->pointPosSpan().end());
    }
    return shape;
}

/// @brief Runs the interface side of a cook until a result is on screen.
bool waitForPublishedCook(
    nt::NetworkManager& manager,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        manager.processPendingCooks();
        if (!manager.isCookBusy() && manager.getPublishedCookResult()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

/// @brief Runs the interface side until the pool has nothing left to do.
void drainCooks(
    nt::NetworkManager& manager,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        manager.processPendingCooks();
        if (!manager.isCookBusy()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

/// @brief Counts how many times the manager told the interface about new geometry.
class GeometrySignalCounter
{
  public:
    GeometrySignalCounter()
    {
        connection_ = nt::nm().displayGeoChanged.connect(
            [this](std::shared_ptr<const NodePacket>) { ++count_; }
        );
    }

    ~GeometrySignalCounter() { connection_.disconnect(); }

    int count() const { return count_; }

  private:
    boost::signals2::scoped_connection connection_;
    std::atomic<int> count_{0};
};

} // namespace

TEST_CASE_METHOD(NetworkReset, "a background cook runs independent nodes at the same time")
{
    nt::NetworkManager& manager = nt::nm();
    probeState().reset();
    probeState().holdMs = 40;
    manager.setAsyncCookingEnabled(true);

    // Eight probes with nothing between them, so they all cook at once.
    std::vector<nt::NodeId> probes;
    for (int i = 0; i < 8; ++i) probes.push_back(manager.createNode(probeTypeName()));

    manager.requestCook(probes);
    REQUIRE(waitForPublishedCook(manager));

    // They ran at the same time, which a single threaded cook could not do.
    REQUIRE(probeState().maxConcurrent() > 1);
    REQUIRE(probeState().cookCount() == 8);

    // Every probe produced its geometry.
    const std::shared_ptr<const async::CookResult> result = manager.getPublishedCookResult();
    REQUIRE(result);
    for (nt::NodeId probe : probes)
    {
        const std::shared_ptr<const NodePacket> packet = result->getOutput(probe, 0);
        REQUIRE(packet);
        REQUIRE(shapeOf(*packet).pointCounts.at(0) == 3);
    }
}

TEST_CASE_METHOD(NetworkReset, "a background cook waits for the nodes it depends on")
{
    nt::NetworkManager& manager = nt::nm();
    probeState().reset();
    probeState().holdMs = 0;
    manager.setAsyncCookingEnabled(true);

    // Two probes feed one grow, and that grow feeds another.
    const nt::NodeId probeA = manager.createNode(probeTypeName());
    const nt::NodeId probeB = manager.createNode(probeTypeName());
    const nt::NodeId growA = manager.createNode(growTypeName());
    const nt::NodeId growB = manager.createNode(growTypeName());

    setIntParameter(probeA, "points", 2);
    setIntParameter(probeB, "points", 5);
    setIntParameter(growA, "grow", 1);
    setIntParameter(growB, "grow", 3);

    manager.connectNodes(probeA, 0, growA, 0);
    manager.connectNodes(probeB, 0, growA, 0);
    manager.connectNodes(growA, 0, growB, 0);

    manager.requestCook({growB});
    REQUIRE(waitForPublishedCook(manager));

    // The result holds both probes and both grows, so nothing was skipped.
    const std::shared_ptr<const async::CookResult> result = manager.getPublishedCookResult();
    REQUIRE(result);
    REQUIRE(result->getOutput(probeA, 0));
    REQUIRE(result->getOutput(probeB, 0));
    REQUIRE(result->getOutput(growA, 0));
    REQUIRE(result->getOutput(growB, 0));

    // The grow read the geometry of both of its inputs, so the cooks ran in
    // the order the wiring asks for.
    const std::shared_ptr<const NodePacket> grown = result->getOutput(growA, 0);
    REQUIRE(grown);
    REQUIRE(shapeOf(*grown).pointCounts.at(0) == 8);
}

TEST_CASE_METHOD(NetworkReset, "a background cook gives the same result as a single threaded one")
{
    nt::NetworkManager& manager = nt::nm();
    probeState().reset();
    probeState().holdMs = 0;

    // The same shape of network is built twice, once for each way of cooking.
    auto buildNetwork = [&]() {
        const nt::NodeId probeA = manager.createNode(probeTypeName());
        const nt::NodeId probeB = manager.createNode(probeTypeName());
        const nt::NodeId growA = manager.createNode(growTypeName());
        const nt::NodeId growB = manager.createNode(growTypeName());

        setIntParameter(probeA, "points", 2);
        setIntParameter(probeB, "points", 5);
        setIntParameter(growA, "grow", 1);
        setIntParameter(growB, "grow", 3);

        manager.connectNodes(probeA, 0, growA, 0);
        manager.connectNodes(probeB, 0, growA, 0);
        manager.connectNodes(growA, 0, growB, 0);
        return growB;
    };

    // The single threaded cook.
    const nt::NodeId singleTarget = buildNetwork();
    manager.cook(singleTarget);
    const GeometryShape singleShape = shapeOf(*manager.getNode(singleTarget).getOutputPacket(0));

    manager._reset();

    // The background cook, over the same network.
    manager.setAsyncCookingEnabled(true);
    const nt::NodeId backgroundTarget = buildNetwork();
    manager.requestCook({backgroundTarget});
    REQUIRE(waitForPublishedCook(manager));

    const std::shared_ptr<const async::CookResult> result = manager.getPublishedCookResult();
    REQUIRE(result);
    const std::shared_ptr<const NodePacket> backgroundPacket = result->getOutput(backgroundTarget, 0);
    REQUIRE(backgroundPacket);

    REQUIRE(shapeOf(*backgroundPacket) == singleShape);
}

TEST_CASE_METHOD(NetworkReset, "a background cook gives the same result however the work is spread")
{
    nt::NetworkManager& manager = nt::nm();
    probeState().reset();
    probeState().holdMs = 0;
    manager.setAsyncCookingEnabled(true);

    // A wide network, so the pool has more than one node to hand out at a time.
    std::vector<nt::NodeId> probes;
    for (int i = 0; i < 16; ++i) probes.push_back(manager.createNode(probeTypeName()));

    const nt::NodeId grow = manager.createNode(growTypeName());
    setIntParameter(grow, "grow", 7);
    for (nt::NodeId probe : probes) manager.connectNodes(probe, 0, grow, 0);

    manager.requestCook({grow});
    REQUIRE(waitForPublishedCook(manager));

    const std::shared_ptr<const async::CookResult> result = manager.getPublishedCookResult();
    REQUIRE(result);
    const std::shared_ptr<const NodePacket> packet = result->getOutput(grow, 0);
    REQUIRE(packet);

    // Sixteen probes of three points each, plus seven grown points.
    REQUIRE(shapeOf(*packet).pointCounts.at(0) == 16 * 3 + 7);
}

TEST_CASE_METHOD(NetworkReset, "an edit during a background cook abandons the cook that is running")
{
    nt::NetworkManager& manager = nt::nm();
    probeState().reset();
    probeState().holdMs = 250;
    manager.setAsyncCookingEnabled(true);

    const nt::NodeId probe = manager.createNode(probeTypeName());
    const nt::NodeId grow = manager.createNode(growTypeName());
    setIntParameter(probe, "points", 4);
    setIntParameter(grow, "grow", 2);
    manager.connectNodes(probe, 0, grow, 0);
    manager.setDisplayNode(grow);

    // The first cook starts and the probe holds it open.
    manager.requestCook({grow});
    const async::CookGeneration firstGeneration = manager.getCookGeneration();

    // The user edits while it runs. The edit abandons the cook that is running
    // rather than queueing a second one behind it.
    setIntParameter(probe, "points", 9);
    REQUIRE(manager.getCookGeneration() != firstGeneration);

    drainCooks(manager);

    // The cook that is on screen is the one that came after the edit.
    REQUIRE(waitForPublishedCook(manager));
    const std::shared_ptr<const async::CookResult> result = manager.getPublishedCookResult();
    REQUIRE(result);
    const std::shared_ptr<const NodePacket> packet = result->getOutput(grow, 0);
    REQUIRE(packet);

    // Nine points from the probe and two from the grow.
    REQUIRE(shapeOf(*packet).pointCounts.at(0) == 11);
}

TEST_CASE_METHOD(NetworkReset, "a burst of edits leaves one background cook behind")
{
    nt::NetworkManager& manager = nt::nm();
    probeState().reset();
    probeState().holdMs = 20;
    manager.setAsyncCookingEnabled(true);

    const nt::NodeId probe = manager.createNode(probeTypeName());
    setIntParameter(probe, "points", 4);
    manager.setDisplayNode(probe);

    GeometrySignalCounter counter;

    // A burst of edits. Only the last one is worth cooking.
    for (int i = 0; i < 6; ++i)
    {
        setIntParameter(probe, "points", 10 + i);
        manager.requestCook({probe});
    }

    drainCooks(manager);
    REQUIRE(waitForPublishedCook(manager));

    // The interface was told about the geometry once, not once per edit, so
    // the cooks that were overtaken never reached the screen.
    REQUIRE(counter.count() == 1);

    const std::shared_ptr<const async::CookResult> result = manager.getPublishedCookResult();
    REQUIRE(result);
    const std::shared_ptr<const NodePacket> packet = result->getOutput(probe, 0);
    REQUIRE(packet);

    // The result is the one the last edit asked for.
    REQUIRE(shapeOf(*packet).pointCounts.at(0) == 15);
}

TEST_CASE_METHOD(NetworkReset, "the last result stays on screen while the next cook runs")
{
    nt::NetworkManager& manager = nt::nm();
    probeState().reset();
    probeState().holdMs = 0;
    manager.setAsyncCookingEnabled(true);

    const nt::NodeId probe = manager.createNode(probeTypeName());
    setIntParameter(probe, "points", 4);
    manager.setDisplayNode(probe);

    manager.requestCook({probe});
    REQUIRE(waitForPublishedCook(manager));

    const std::shared_ptr<const async::CookResult> first = manager.getPublishedCookResult();
    REQUIRE(first);
    REQUIRE(shapeOf(*first->getOutput(probe, 0)).pointCounts.at(0) == 4);

    // The next cook runs, and the result on screen is still the old one until
    // the cook is done. The cook is held for much longer than the loop below
    // lasts (about 40 ms), so a runner that stalls for a while in the loop does
    // not let the cook finish first.
    probeState().holdMs = 1000;
    setIntParameter(probe, "points", 8);
    manager.requestCook({probe});

    for (int i = 0; i < 20; ++i)
    {
        manager.processPendingCooks();
        const std::shared_ptr<const async::CookResult> current =
            manager.getPublishedCookResult();
        REQUIRE(current);
        REQUIRE(current == first);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    REQUIRE(waitForPublishedCook(manager));
    const std::shared_ptr<const async::CookResult> second = manager.getPublishedCookResult();
    REQUIRE(second);
    REQUIRE(second != first);
    REQUIRE(shapeOf(*second->getOutput(probe, 0)).pointCounts.at(0) == 8);
}

TEST_CASE_METHOD(NetworkReset, "a background cook can be abandoned on purpose")
{
    nt::NetworkManager& manager = nt::nm();
    probeState().reset();
    probeState().holdMs = 200;
    manager.setAsyncCookingEnabled(true);

    const nt::NodeId probe = manager.createNode(probeTypeName());
    manager.setDisplayNode(probe);

    manager.requestCook({probe});
    REQUIRE(manager.isCookBusy());

    manager.cancelCook();
    drainCooks(manager);

    // Nothing was published, because the cook was abandoned.
    REQUIRE(!manager.getPublishedCookResult());
}
