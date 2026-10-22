#include "Engine/Core/Types.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeLoader.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/NetworkGraph/NetworkGraph.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Simulation/KeyframeStore.h"
#include "Engine/Simulation/ParticleSolver.h"
#include "Engine/Simulation/SpringSolver.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace enzo;

namespace {

// Puts the scene back the way it was, so one test does not leave keyframes
// behind for the next.
struct SimulationReset
{
    SimulationReset()
    {
        nt::nm()._reset();
        sim::KeyframeStore::instance().clear();
    }
    ~SimulationReset()
    {
        sim::KeyframeStore::instance().clear();
        nt::nm()._reset();
    }
};

// Creates a node without cooking it.
nt::NodeId addNode(const char* typeName)
{
    nt::NodeLoader::loadNodes();
    return nt::nm().createNode(typeName);
}

// Returns the mesh a node cooked.
std::shared_ptr<const geo::Mesh> getMesh(nt::Node& node, unsigned int outputIndex)
{
    const auto mesh =
        std::dynamic_pointer_cast<const geo::Mesh>(node.getOutputPacket(outputIndex)->getPrimitive(0));
    REQUIRE(mesh != nullptr);
    return mesh;
}

} // namespace

TEST_CASE_METHOD(SimulationReset, "A keyframe track moves between two keys in a straight line")
{
    sim::KeyframeTrack track;
    track.addKeyframe(1.f, 0.f);
    track.addKeyframe(11.f, 10.f);

    REQUIRE(track.evaluate(1.f) == Catch::Approx(0.f));
    REQUIRE(track.evaluate(6.f) == Catch::Approx(5.f));
    REQUIRE(track.evaluate(11.f) == Catch::Approx(10.f));
}

TEST_CASE_METHOD(SimulationReset, "A keyframe track holds its value past its ends")
{
    sim::KeyframeTrack track;
    track.addKeyframe(5.f, 3.f);
    track.addKeyframe(10.f, 7.f);

    // Before the first key and after the last one the value is held.
    REQUIRE(track.evaluate(0.f) == Catch::Approx(3.f));
    REQUIRE(track.evaluate(20.f) == Catch::Approx(7.f));
}

TEST_CASE_METHOD(SimulationReset, "A keyframe track steps when the key says so")
{
    sim::KeyframeTrack track;
    track.addKeyframe(1.f, 0.f, sim::Interpolation::Step);
    track.addKeyframe(11.f, 10.f);

    REQUIRE(track.evaluate(5.f) == Catch::Approx(0.f));
    REQUIRE(track.evaluate(10.9f) == Catch::Approx(0.f));
    REQUIRE(track.evaluate(11.f) == Catch::Approx(10.f));
}

TEST_CASE_METHOD(SimulationReset, "A keyframe track eases in and out when the key says so")
{
    sim::KeyframeTrack track;
    track.addKeyframe(1.f, 0.f, sim::Interpolation::Smooth);
    track.addKeyframe(11.f, 10.f);

    // A smooth step sits below the straight line in the first half of the
    // segment and above it in the second. The two meet in the middle of the
    // segment, so the check is made away from it.
    REQUIRE(track.evaluate(4.f) < 3.f);
    REQUIRE(track.evaluate(4.f) > 1.f);
    REQUIRE(track.evaluate(8.f) > 7.f);
    REQUIRE(track.evaluate(1.f) == Catch::Approx(0.f));
    REQUIRE(track.evaluate(11.f) == Catch::Approx(10.f));
}

TEST_CASE_METHOD(SimulationReset, "A keyframe track gives the same value every time it is asked")
{
    sim::KeyframeTrack track;
    track.addKeyframe(1.f, 0.f);
    track.addKeyframe(7.f, 2.5f);
    track.addKeyframe(19.f, -3.f);

    const float first = track.evaluate(4.5f);
    for (int attempt = 0; attempt < 5; ++attempt)
        REQUIRE(track.evaluate(4.5f) == first);
}

TEST_CASE_METHOD(SimulationReset, "The store keeps the keyframes of a parameter")
{
    auto& store = sim::KeyframeStore::instance();
    const sim::KeyframeTarget target{1, "translate", 0};

    store.setKeyframe(target, 1.f, 0.f);
    store.setKeyframe(target, 11.f, 10.f);

    REQUIRE(store.hasTrack(target));
    REQUIRE(store.getTrack(target)->size() == 2);
    REQUIRE(store.evaluate(target, 6.f).value() == Catch::Approx(5.f));
}

TEST_CASE_METHOD(SimulationReset, "A keyframed parameter takes the value its track holds")
{
    auto& nm = nt::nm();
    const nt::NodeId nodeId = addNode("enzo::transform");
    auto translate = nm.getNode(nodeId).getParameter("translate").lock();

    auto& store = sim::KeyframeStore::instance();
    const sim::KeyframeTarget target{nodeId, "translate", 0};
    store.setKeyframe(target, 1.f, 0.f);
    store.setKeyframe(target, 11.f, 10.f);

    // The keyframe is put on the frame the scene sits on, so the value follows
    // the frame.
    nm.setFrame(6);
    REQUIRE(translate->evalFloat() == Catch::Approx(5.f));

    nm.setFrame(11);
    REQUIRE(translate->evalFloat() == Catch::Approx(10.f));

    nm.setFrame(1);
    REQUIRE(translate->evalFloat() == Catch::Approx(0.f));
}

TEST_CASE_METHOD(SimulationReset, "A keyframed parameter is marked as reading the time")
{
    auto& nm = nt::nm();
    const nt::NodeId nodeId = addNode("enzo::transform");

    auto& store = sim::KeyframeStore::instance();
    store.setKeyframe({nodeId, "translate", 0}, 1.f, 0.f);

    const nt::Unit expected{nodeId, "translate", 0};
    bool found = false;
    for (const nt::Unit& unit : nm.graph().getTimeDependents())
        if (unit == expected) found = true;
    REQUIRE(found);
}

TEST_CASE_METHOD(SimulationReset, "Taking the keyframes away puts the old value back")
{
    auto& nm = nt::nm();
    const nt::NodeId nodeId = addNode("enzo::transform");
    auto translate = nm.getNode(nodeId).getParameter("translate").lock();
    translate->setFloat(3.f);

    auto& store = sim::KeyframeStore::instance();
    store.setKeyframe({nodeId, "translate", 0}, 1.f, 8.f);
    REQUIRE(translate->evalFloat() == Catch::Approx(8.f));

    store.clearTrack({nodeId, "translate", 0});
    REQUIRE(translate->evalFloat() == Catch::Approx(3.f));
    REQUIRE_FALSE(store.hasTrack({nodeId, "translate", 0}));
}

TEST_CASE_METHOD(SimulationReset, "A parameter that holds keyframes gives the same value every time")
{
    auto& nm = nt::nm();
    const nt::NodeId nodeId = addNode("enzo::transform");

    auto& store = sim::KeyframeStore::instance();
    store.setKeyframe({nodeId, "translate", 0}, 1.f, 0.f);
    store.setKeyframe({nodeId, "translate", 0}, 11.f, 10.f);

    nm.setFrame(7);
    const float first = nm.getNode(nodeId).getParameter("translate").lock()->evalFloat();

    nm.setFrame(2);
    nm.setFrame(7);
    REQUIRE(nm.getNode(nodeId).getParameter("translate").lock()->evalFloat() == first);
}

TEST_CASE_METHOD(SimulationReset, "A particle falls under gravity")
{
    sim::ParticleSettings settings;
    settings.count = 1;
    settings.gravity = Vector3(0.f, -10.f, 0.f);
    settings.drag = 0.f;

    sim::ParticleSolver solver;
    solver.reset(settings);

    // Each particle starts where its own hash puts it, inside the box of the
    // settings, so the step is measured from the place it started at.
    const float startY = solver.getParticles()[0].position.y();
    solver.step(settings, 0.1f);

    REQUIRE(solver.getParticles().size() == 1);
    REQUIRE(solver.getParticles()[0].velocity.y() == Catch::Approx(-1.f));
    REQUIRE(solver.getParticles()[0].position.y() == Catch::Approx(startY - 0.1f));
}

TEST_CASE_METHOD(SimulationReset, "The particles start in the same places every time")
{
    sim::ParticleSettings settings;
    settings.count = 16;

    sim::ParticleSolver first;
    sim::ParticleSolver second;
    first.reset(settings);
    second.reset(settings);

    REQUIRE(first.size() == second.size());
    for (std::size_t index = 0; index < first.size(); ++index)
        REQUIRE(first.getParticles()[index].position.isApprox(second.getParticles()[index].position));
}

TEST_CASE_METHOD(SimulationReset, "The particles are the same however the frames were reached")
{
    sim::ParticleSettings settings;
    settings.count = 8;
    settings.gravity = Vector3(0.f, -9.81f, 0.f);

    const auto run = [&settings](floatT endFrame) {
        return sim::simulateParticles(
            settings,
            [settings](floatT) { return settings; },
            1.f,
            endFrame,
            24.f,
            1
        );
    };

    // Walking to the frame in one go gives the same particles as walking there
    // a step at a time.
    const auto straight = run(10.f);
    run(4.f);
    run(7.f);
    const auto stepped = run(10.f);

    REQUIRE(straight.size() == stepped.size());
    for (std::size_t index = 0; index < straight.size(); ++index)
        REQUIRE(straight[index].position.isApprox(stepped[index].position));
}

TEST_CASE_METHOD(SimulationReset, "A spring sheet hangs from its top row")
{
    sim::SpringSettings settings;
    settings.columns = 5;
    settings.rows = 5;
    settings.spacing = 1.f;
    settings.gravity = Vector3(0.f, -9.81f, 0.f);

    sim::SpringSolver solver;
    solver.reset(settings);
    const Vector3 topBefore = solver.getPositions()[0];

    for (int step = 0; step < 24; ++step)
        solver.step(settings, 1.f / 24.f);

    // The held row has not moved, and the row below it has fallen.
    REQUIRE(solver.getPositions()[0].isApprox(topBefore));
    REQUIRE(solver.getPositions()[solver.size() - 1].y() < topBefore.y() - 0.5f);
}

TEST_CASE_METHOD(SimulationReset, "The spring sheet is the same however the frames were reached")
{
    sim::SpringSettings settings;
    settings.columns = 4;
    settings.rows = 4;
    settings.gravity = Vector3(0.f, -9.81f, 0.f);

    const auto run = [&settings](floatT endFrame) {
        return sim::simulateSprings(
            settings,
            [settings](floatT) { return settings; },
            1.f,
            endFrame,
            24.f,
            1
        );
    };

    const auto straight = run(12.f);
    run(5.f);
    run(9.f);
    const auto stepped = run(12.f);

    REQUIRE(straight.size() == stepped.size());
    for (std::size_t index = 0; index < straight.size(); ++index)
        REQUIRE(straight[index].isApprox(stepped[index]));
}

TEST_CASE_METHOD(SimulationReset, "The simulation node makes as many particles as it says")
{
    auto& nm = nt::nm();
    const nt::NodeId nodeId = addNode("enzo::simulation");

    nm.getNode(nodeId).getParameter("particle_count").lock()->setInt(12);
    nm.cook(nodeId);

    const auto mesh = getMesh(nm.getNode(nodeId), 0);
    REQUIRE(mesh->getNumPoints() == 12);
}

TEST_CASE_METHOD(SimulationReset, "The simulation node makes a spring sheet of the size it says")
{
    auto& nm = nt::nm();
    const nt::NodeId nodeId = addNode("enzo::simulation");

    nm.getNode(nodeId).getParameter("spring_columns").lock()->setInt(5);
    nm.getNode(nodeId).getParameter("spring_rows").lock()->setInt(3);
    nm.cook(nodeId);

    const auto mesh = getMesh(nm.getNode(nodeId), 1);
    REQUIRE(mesh->getNumPoints() == 15);
}

TEST_CASE_METHOD(SimulationReset, "The simulation node cooks again when the frame moves")
{
    auto& nm = nt::nm();
    const nt::NodeId nodeId = addNode("enzo::simulation");

    nm.getNode(nodeId).getParameter("particle_count").lock()->setInt(4);
    nm.setFrame(1);
    nm.cook(nodeId);
    REQUIRE_FALSE(nm.getNode(nodeId).isDirty());

    // The node read the frame, so moving it dirties the node again.
    nm.setFrame(20);
    REQUIRE(nm.getNode(nodeId).isDirty());
}

TEST_CASE_METHOD(SimulationReset, "Keyframing a setting of the simulation node moves the simulation")
{
    auto& nm = nt::nm();
    const nt::NodeId nodeId = addNode("enzo::simulation");

    nm.getNode(nodeId).getParameter("particle_count").lock()->setInt(1);
    nm.getNode(nodeId).getParameter("particle_gravity").lock()->setFloat(0.f, 1);
    nm.getNode(nodeId).getParameter("particle_drag").lock()->setFloat(0.f);

    // The pull is keyed in over the first ten frames.
    auto& store = sim::KeyframeStore::instance();
    store.setKeyframe({nodeId, "particle_gravity", 1}, 1.f, 0.f);
    store.setKeyframe({nodeId, "particle_gravity", 1}, 11.f, -20.f);

    nm.setFrame(1);
    nm.cook(nodeId);
    const auto atStart = getMesh(nm.getNode(nodeId), 0);

    nm.setFrame(11);
    nm.cook(nodeId);
    const auto atEnd = getMesh(nm.getNode(nodeId), 0);

    // The particle has fallen further by the end of the keyed pull than it had
    // at the start of it.
    REQUIRE(atEnd->getPointPos(0).y() < atStart->getPointPos(0).y() - 0.5f);
}

TEST_CASE_METHOD(SimulationReset, "The simulation node gives the same points twice")
{
    auto& nm = nt::nm();
    const nt::NodeId nodeId = addNode("enzo::simulation");

    nm.getNode(nodeId).getParameter("particle_count").lock()->setInt(6);
    nm.setFrame(15);
    nm.cook(nodeId);
    const auto first = getMesh(nm.getNode(nodeId), 0);

    // Cook it again from scratch, at the same frame.
    nm.setFrame(3);
    nm.cook(nodeId);
    nm.setFrame(15);
    nm.cook(nodeId);
    const auto second = getMesh(nm.getNode(nodeId), 0);

    REQUIRE(first->getNumPoints() == second->getNumPoints());
    for (int pointIndex = 0; pointIndex < static_cast<int>(first->getNumPoints()); ++pointIndex)
        REQUIRE(first->getPointPos(pointIndex).isApprox(second->getPointPos(pointIndex)));
}
