// The cook cache: what a node that moves with the time keeps of what it produced, so a frame
// already visited costs a lookup. The thing every test here leans on is that a cached cook is the
// same, bit for bit, as a cook made from nothing.

#include "Engine/Core/Types.h"
#include "Engine/Network/CookCache.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/Node.h"
#include "Engine/Network/NodeLoader.h"
#include "Engine/Network/NodeManifest.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Network/NodeTypeTable.h"
#include "Engine/NetworkGraph/NetworkGraph.h"
#include "Engine/Parameter/NodeParameter.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Simulation/KeyframeStore.h"
#include "Engine/Simulation/ParticleSolver.h"
#include "Engine/Simulation/SpringSolver.h"
#include "HelixCore/HelixCore.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace helix;

namespace {

// Puts the scene, the keyframes and the cache back the way they were, so one test does not leave
// anything behind for the next.
struct CacheReset
{
    CacheReset()
    {
        nt::NodeLoader::loadNodes();
        nt::nm()._reset();
        sim::KeyframeStore::instance().clear();
        nt::cookCache().setBudget(nt::CookCache::kDefaultBudget);
        nt::cookCache().clear();
    }
    ~CacheReset()
    {
        sim::KeyframeStore::instance().clear();
        nt::nm()._reset();
        nt::cookCache().setBudget(nt::CookCache::kDefaultBudget);
        nt::cookCache().clear();
    }
};

nt::NodeId addNode(const char* typeName) { return nt::nm().createNode(typeName); }

std::shared_ptr<prm::NodeParameter> parameterOf(nt::NodeId node, const char* name)
{
    return nt::nm().getNode(node).getParameter(name).lock();
}

// Moves the scene to a frame and cooks a node there.
void cookAt(nt::NodeId node, float frame)
{
    nt::nm().setFrame(frame);
    nt::nm().cook(node);
}

// Everything a node produced that a comparison can tell apart, as the bytes of its points.
std::vector<float> bytesOf(nt::NodeId node, unsigned int output = 0)
{
    std::vector<float> out;
    const auto packet = nt::nm().getNode(node).getOutputPacket(output);
    for (const auto& primitive : packet->getPrimitives())
    {
        const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(primitive);
        if (!mesh) continue;
        out.push_back(static_cast<float>(mesh->getNumPoints()));
        out.push_back(static_cast<float>(mesh->getNumFaces()));
        for (Offset point = 0; point < mesh->getNumPoints(); ++point)
        {
            const Vector3 position = mesh->getPointPos(point);
            out.push_back(position.x());
            out.push_back(position.y());
            out.push_back(position.z());
        }
    }
    return out;
}

bool sameBytes(const std::vector<float>& first, const std::vector<float>& second)
{
    return first.size() == second.size() &&
           (first.empty() || std::memcmp(first.data(), second.data(), first.size() * sizeof(float)) == 0);
}

// A grid moved by a keyed transform and by a transform whose height is an expression of the
// frame, and a simulation with keyed gravity followed by a transform. Every node here moves with
// the time.
struct Scene
{
    nt::NodeId grid = 0;
    nt::NodeId keyed = 0;
    nt::NodeId expression = 0;
    nt::NodeId simulation = 0;
    nt::NodeId afterSimulation = 0;

    std::vector<nt::NodeId> animated() const { return {keyed, expression, simulation, afterSimulation}; }
};

Scene buildScene()
{
    auto& nm = nt::nm();
    Scene scene;

    scene.grid = addNode("helix::grid");
    parameterOf(scene.grid, "rows")->setInt(20);
    parameterOf(scene.grid, "columns")->setInt(20);

    scene.keyed = addNode("helix::transform");
    nm.connectNodes(scene.grid, 0, scene.keyed, 0);
    auto& store = sim::KeyframeStore::instance();
    store.setKeyframe({scene.keyed, "translate", 0}, 1.f, 0.f, sim::Interpolation::Smooth);
    store.setKeyframe({scene.keyed, "translate", 0}, 60.f, 12.f, sim::Interpolation::Linear);

    scene.expression = addNode("helix::transform");
    nm.connectNodes(scene.keyed, 0, scene.expression, 0);
    parameterOf(scene.expression, "translate")->setExpression("frame() * 0.25", 1);

    scene.simulation = addNode("helix::simulation");
    parameterOf(scene.simulation, "particle_count")->setInt(300);
    parameterOf(scene.simulation, "particle_swirl")->setFloat(2.f);
    store.setKeyframe({scene.simulation, "particle_gravity", 1}, 1.f, -2.f, sim::Interpolation::Linear);
    store.setKeyframe({scene.simulation, "particle_gravity", 1}, 40.f, -12.f, sim::Interpolation::Linear);

    scene.afterSimulation = addNode("helix::transform");
    nm.connectNodes(scene.simulation, 0, scene.afterSimulation, 0);
    parameterOf(scene.afterSimulation, "uniform_scale")->setFloat(1.5f);
    return scene;
}

// The frames a sweep visits, in an order that goes forward, back, and jumps about.
std::vector<float> visitOrder()
{
    std::vector<float> order;
    for (int frame = 1; frame <= 30; ++frame) order.push_back(static_cast<float>(frame));
    for (int frame = 30; frame >= 1; --frame) order.push_back(static_cast<float>(frame));
    for (const float frame : {17.f, 3.f, 25.f, 25.5f, 9.f, 40.f, 2.f, 33.f, 33.f, 12.f, 61.f, 1.f})
        order.push_back(frame);
    return order;
}

using Visits = std::vector<std::map<nt::NodeId, std::vector<float>>>;

// Cooks every animated node at every frame of the order and keeps what each produced.
Visits sweep(const Scene& scene, const std::vector<float>& order)
{
    Visits visits;
    for (const float frame : order)
    {
        nt::nm().setFrame(frame);
        std::map<nt::NodeId, std::vector<float>> atFrame;
        for (const nt::NodeId node : scene.animated())
        {
            nt::nm().cook(node);
            atFrame[node] = bytesOf(node);
        }
        visits.push_back(std::move(atFrame));
    }
    return visits;
}

} // namespace

TEST_CASE_METHOD(CacheReset, "A cached cook is the same, bit for bit, as a cook made from nothing", "[cookcache]")
{
    const Scene scene = buildScene();
    const std::vector<float> order = visitOrder();

    // With the cache on: forward, back, and jumps. Nothing is shared with the run that follows.
    const Visits cached = sweep(scene, order);
    REQUIRE(nt::cookCache().stats().hits > 0);
    REQUIRE(nt::cookCache().stats().frameEntries > 0);

    // With the cache off, every cook is made from nothing.
    nt::cookCache().setBudget(0);
    const Visits fresh = sweep(scene, order);
    REQUIRE(nt::cookCache().stats().entries == 0);

    REQUIRE(cached.size() == fresh.size());
    for (std::size_t visit = 0; visit < order.size(); ++visit)
        for (const nt::NodeId node : scene.animated())
        {
            INFO("frame " << order[visit] << " node " << nt::nm().getNode(node).getName());
            REQUIRE(sameBytes(cached[visit].at(node), fresh[visit].at(node)));
        }
}

TEST_CASE_METHOD(CacheReset, "Going back to a frame gives the cooks it had, and costs no cook", "[cookcache]")
{
    const Scene scene = buildScene();
    auto& nm = nt::nm();

    cookAt(scene.keyed, 10.f);
    const auto atTen = nm.getNode(scene.keyed).getOutputPacket(0);
    const std::uint64_t generationAtTen = nm.getNode(scene.keyed).getCookGeneration();

    cookAt(scene.keyed, 20.f);
    REQUIRE(nm.getNode(scene.keyed).getOutputPacket(0) != atTen);

    const std::uint64_t hitsBefore = nt::cookCache().stats().hits;
    cookAt(scene.keyed, 10.f);

    // The very packet the first visit made came back.
    REQUIRE(nm.getNode(scene.keyed).getOutputPacket(0) == atTen);
    REQUIRE(nt::cookCache().stats().hits == hitsBefore + 1);

    // A node that was restored counts as cooked, since what it shows changed with it.
    REQUIRE(nm.getNode(scene.keyed).getCookGeneration() != generationAtTen);
    REQUIRE(nm.getNode(scene.keyed).getCookGeneration() != 0);
}

TEST_CASE_METHOD(CacheReset, "A node restored from the cache keeps moving with the frame", "[cookcache]")
{
    const Scene scene = buildScene();
    auto& nm = nt::nm();

    cookAt(scene.expression, 5.f);
    cookAt(scene.expression, 6.f);
    cookAt(scene.expression, 5.f);
    REQUIRE_FALSE(nm.getNode(scene.expression).isDirty());

    // The restored cook did not read the frame, so the graph would have forgotten the node.
    nm.setFrame(8.f);
    REQUIRE(nm.getNode(scene.expression).isDirty());
    REQUIRE(nm.getNode(scene.simulation).isDirty());
    nm.cook(scene.simulation);
    nm.setFrame(5.f);
    nm.cook(scene.simulation);
    nm.setFrame(9.f);
    REQUIRE(nm.getNode(scene.simulation).isDirty());
}

TEST_CASE_METHOD(CacheReset, "A simulation that nothing animates keeps moving with the frame after a restore", "[cookcache]")
{
    // No keyframe and no expression: the only thing that ties this node to the time is that its
    // cook reads the frame, which a cook answered from the cache does not do.
    auto& nm = nt::nm();
    const nt::NodeId plain = addNode("helix::simulation");
    parameterOf(plain, "particle_count")->setInt(40);
    REQUIRE_FALSE(nm.graph().hasTimeDependentParameter(plain));

    cookAt(plain, 5.f);
    cookAt(plain, 6.f);
    const std::uint64_t hitsBefore = nt::cookCache().stats().hits;
    cookAt(plain, 5.f);
    REQUIRE(nt::cookCache().stats().hits == hitsBefore + 1);
    REQUIRE_FALSE(nm.getNode(plain).isDirty());

    nm.setFrame(8.f);
    REQUIRE(nm.getNode(plain).isDirty());
    REQUIRE(nm.getNode(plain).readsTime());
}

TEST_CASE_METHOD(CacheReset, "A frame between two frames is kept like any other", "[cookcache]")
{
    const Scene scene = buildScene();
    auto& nm = nt::nm();

    cookAt(scene.simulation, 12.5f);
    const std::vector<float> first = bytesOf(scene.simulation);
    cookAt(scene.simulation, 13.f);

    const std::uint64_t hitsBefore = nt::cookCache().stats().hits;
    cookAt(scene.simulation, 12.5f);
    REQUIRE(nt::cookCache().stats().hits == hitsBefore + 1);
    REQUIRE(sameBytes(first, bytesOf(scene.simulation)));

    // Only whole frames are reported for the ruler.
    const std::vector<int> frames = nt::cookCache().cachedFrames(scene.simulation);
    REQUIRE(frames == std::vector<int>{13});
}

TEST_CASE_METHOD(CacheReset, "Only a node that moves with the time is kept", "[cookcache]")
{
    const Scene scene = buildScene();

    cookAt(scene.grid, 4.f);
    cookAt(scene.grid, 5.f);
    REQUIRE(nt::cookCache().cachedFrames(scene.grid).empty());

    // The node after the grid moves with the time through its own keyframes.
    cookAt(scene.keyed, 4.f);
    cookAt(scene.keyed, 5.f);
    REQUIRE(nt::cookCache().cachedFrames(scene.keyed) == std::vector<int>{4, 5});

    // A node with nothing to do with the time is never filed, whatever is wired after it.
    const nt::NodeId still = addNode("helix::transform");
    nt::nm().connectNodes(scene.grid, 0, still, 0);
    cookAt(still, 6.f);
    cookAt(still, 7.f);
    REQUIRE(nt::cookCache().cachedFrames(still).empty());
}

TEST_CASE_METHOD(CacheReset, "What is wired after a node that moves with the time is kept at every frame", "[cookcache]")
{
    const Scene scene = buildScene();
    auto& nm = nt::nm();

    for (const float frame : {3.f, 4.f, 5.f}) cookAt(scene.afterSimulation, frame);
    REQUIRE(nt::cookCache().cachedFrames(scene.afterSimulation) == std::vector<int>{3, 4, 5});

    const std::vector<float> atFour = [&] {
        cookAt(scene.afterSimulation, 4.f);
        return bytesOf(scene.afterSimulation);
    }();
    const auto packet = nm.getNode(scene.afterSimulation).getOutputPacket(0);

    // Going back to a visited frame restores the simulation from the cache first, so the
    // transform sees the packet it saw then and restores too.
    cookAt(scene.afterSimulation, 5.f);
    const std::uint64_t hitsBefore = nt::cookCache().stats().hits;
    cookAt(scene.afterSimulation, 4.f);
    REQUIRE(nt::cookCache().stats().hits == hitsBefore + 2);
    REQUIRE(nm.getNode(scene.afterSimulation).getOutputPacket(0) == packet);
    REQUIRE(sameBytes(atFour, bytesOf(scene.afterSimulation)));
}

TEST_CASE_METHOD(CacheReset, "Editing a node drops what it and what follows it kept", "[cookcache]")
{
    const Scene scene = buildScene();
    auto& nm = nt::nm();

    for (const float frame : {2.f, 3.f, 4.f}) cookAt(scene.expression, frame);
    REQUIRE(nt::cookCache().cachedFrames(scene.keyed).size() == 3);
    REQUIRE(nt::cookCache().cachedFrames(scene.expression).size() == 3);

    // An edit upstream of both. Neither is kept, and the one that is cooked next shows the edit.
    const std::vector<float> before = bytesOf(scene.expression);
    parameterOf(scene.grid, "rows")->setInt(8);
    REQUIRE(nt::cookCache().cachedFrames(scene.keyed).empty());
    REQUIRE(nt::cookCache().cachedFrames(scene.expression).empty());

    cookAt(scene.expression, 4.f);
    REQUIRE_FALSE(sameBytes(before, bytesOf(scene.expression)));
    REQUIRE(nt::cookCache().cachedFrames(scene.expression) == std::vector<int>{4});

    // An edit that wires another node in leaves nothing stale either.
    cookAt(scene.expression, 3.f);
    const nt::NodeId other = addNode("helix::grid");
    parameterOf(other, "rows")->setInt(3);
    parameterOf(other, "columns")->setInt(3);
    nm.connectNodes(other, 0, scene.keyed, 0);
    REQUIRE(nt::cookCache().cachedFrames(scene.keyed).empty());
    cookAt(scene.expression, 3.f);
    REQUIRE(bytesOf(scene.expression)[0] == 9.f);
}

TEST_CASE_METHOD(CacheReset, "Changing a keyframe far from the playhead is not answered from the cache", "[cookcache]")
{
    const Scene scene = buildScene();
    auto& store = sim::KeyframeStore::instance();

    // Visit frame 40, on the way to the last key, and go back to the first frame. The node
    // wired after it is only as old as the packet it was made from.
    cookAt(scene.expression, 40.f);
    const std::vector<float> before = bytesOf(scene.keyed);
    const std::vector<float> beforeAfter = bytesOf(scene.expression);
    cookAt(scene.expression, 1.f);

    // Moving the last key moves frame 40, and leaves the value at frame 1 where it was, so the
    // parameter is not written, nothing dirties the node and nothing drops what it kept. Only
    // the keys in the key of the cook tell the visit to frame 40 from the one before the edit.
    store.setKeyframe({scene.keyed, "translate", 0}, 60.f, 40.f, sim::Interpolation::Linear);
    REQUIRE(nt::cookCache().cachedFrames(scene.keyed) == std::vector<int>{1, 40});
    REQUIRE(nt::cookCache().cachedFrames(scene.expression) == std::vector<int>{1, 40});

    cookAt(scene.expression, 40.f);
    REQUIRE_FALSE(sameBytes(before, bytesOf(scene.keyed)));
    REQUIRE_FALSE(sameBytes(beforeAfter, bytesOf(scene.expression)));

    // And the new values are the ones a cook from nothing gives.
    const std::vector<float> cached = bytesOf(scene.keyed);
    const std::vector<float> cachedAfter = bytesOf(scene.expression);
    nt::cookCache().setBudget(0);
    cookAt(scene.expression, 3.f);
    cookAt(scene.expression, 40.f);
    REQUIRE(sameBytes(cached, bytesOf(scene.keyed)));
    REQUIRE(sameBytes(cachedAfter, bytesOf(scene.expression)));
}

TEST_CASE_METHOD(CacheReset, "Changing the frame rate drops what a simulation made at the old one", "[cookcache]")
{
    const Scene scene = buildScene();
    auto& nm = nt::nm();

    cookAt(scene.simulation, 20.f);
    const std::vector<float> at24 = bytesOf(scene.simulation);
    REQUIRE(nt::cookCache().stats().entries > 0);

    nm.setFps(30.f);
    REQUIRE(nt::cookCache().stats().entries == 0);

    nm.setFrame(20.f);
    nm.cook(scene.simulation);
    REQUIRE_FALSE(sameBytes(at24, bytesOf(scene.simulation)));

    nm.setFps(24.f);
    nm.setFrame(20.f);
    nm.cook(scene.simulation);
    REQUIRE(sameBytes(at24, bytesOf(scene.simulation)));
}

TEST_CASE_METHOD(CacheReset, "The budget bounds the cache, and the entry used longest ago goes first", "[cookcache]")
{
    const Scene scene = buildScene();
    auto& nm = nt::nm();

    cookAt(scene.keyed, 1.f);
    const std::size_t perEntry = nt::cookCache().stats().bytesUsed;
    REQUIRE(perEntry > 0);
    nt::cookCache().clear();

    // Room for three entries and not a fourth.
    nt::cookCache().setBudget(perEntry * 3 + perEntry / 2);
    for (int frame = 1; frame <= 8; ++frame) cookAt(scene.keyed, static_cast<float>(frame));

    const nt::CookCacheStats stats = nt::cookCache().stats();
    REQUIRE(stats.bytesUsed <= stats.budget);
    REQUIRE(stats.frameEntries == 3);
    REQUIRE(nt::cookCache().cachedFrames(scene.keyed) == std::vector<int>{6, 7, 8});

    // Looking at frame 6 makes it the newest, so the next cook pushes out frame 7.
    cookAt(scene.keyed, 6.f);
    cookAt(scene.keyed, 1.f);
    REQUIRE(nt::cookCache().cachedFrames(scene.keyed) == std::vector<int>{1, 6, 8});

    // A smaller budget drops what no longer fits at once.
    nt::cookCache().setBudget(perEntry + perEntry / 2);
    REQUIRE(nt::cookCache().stats().frameEntries == 1);

    // And zero turns it off.
    nt::cookCache().setBudget(0);
    REQUIRE(nt::cookCache().stats().entries == 0);
    cookAt(scene.keyed, 9.f);
    REQUIRE(nt::cookCache().stats().entries == 0);
    (void) nm;
}

TEST_CASE_METHOD(CacheReset, "A node that opts out is not kept, and nor is what moves with it", "[cookcache]")
{
    const Scene scene = buildScene();

    // `cache: false` in a node.yaml.
    const nt::NodeType& type = nt::nm().getNode(scene.keyed).getType();
    auto& mutableType = const_cast<nt::NodeType&>(type);
    mutableType.cacheable = false;

    struct Restore
    {
        nt::NodeType& type;
        ~Restore() { type.cacheable = true; }
    } restore{mutableType};

    const std::uint64_t hitsBefore = nt::cookCache().stats().hits;
    cookAt(scene.expression, 4.f);
    cookAt(scene.expression, 5.f);
    cookAt(scene.expression, 4.f);

    // Both transforms are of that type. The first opted out, and the second moves with it.
    REQUIRE(nt::cookCache().cachedFrames(scene.keyed).empty());
    REQUIRE(nt::cookCache().cachedFrames(scene.expression).empty());
    REQUIRE(nt::cookCache().stats().hits == hitsBefore);
}

TEST_CASE("A node.yaml turns the cache off with cache: false", "[cookcache]")
{
    const std::string manifest = R"(
version: 1
name: reader
namespace: helix
implementation:
  kind: cpp
  library: helixOps
)";
    REQUIRE(nt::NodeManifest::loadFromString(manifest).getNodeType().cacheable);
    REQUIRE_FALSE(nt::NodeManifest::loadFromString(manifest + "cache: false\n").getNodeType().cacheable);
    REQUIRE(nt::NodeManifest::loadFromString(manifest + "cache: true\n").getNodeType().cacheable);
}

TEST_CASE_METHOD(CacheReset, "A simulation resumed from a saved state is the same as one run from the first frame", "[cookcache]")
{
    const Scene scene = buildScene();
    auto& nm = nt::nm();

    // Frame 61 is reached from the first frame, leaving states behind every eight frames.
    cookAt(scene.simulation, 61.f);
    REQUIRE(nt::cookCache().stats().stateEntries >= 7);

    // The same frames from nothing.
    std::map<float, std::vector<float>> fresh;
    nt::cookCache().setBudget(0);
    for (const float frame : {1.f, 9.f, 17.f, 30.5f, 44.f, 61.f})
    {
        cookAt(scene.simulation, frame);
        fresh[frame] = bytesOf(scene.simulation);
    }
    // And the springs, the second output of the node.
    std::map<float, std::vector<float>> freshSprings;
    for (const float frame : {1.f, 9.f, 17.f, 30.5f, 44.f, 61.f})
    {
        cookAt(scene.simulation, frame);
        freshSprings[frame] = bytesOf(scene.simulation, 1);
    }

    // The cache is on again, and the frames are visited in an order that jumps back and forth.
    nt::cookCache().setBudget(nt::CookCache::kDefaultBudget);
    nt::cookCache().clear();
    for (const float frame : {44.f, 9.f, 61.f, 1.f, 30.5f, 17.f, 44.f, 61.f, 9.f})
    {
        // Only the states are kept between visits to a frame that was never cooked: put the
        // frame itself out of the cache so the cook has to resume from a state.
        cookAt(scene.simulation, frame);
        INFO("frame " << frame);
        REQUIRE(sameBytes(fresh.at(frame), bytesOf(scene.simulation)));
        REQUIRE(sameBytes(freshSprings.at(frame), bytesOf(scene.simulation, 1)));
    }
    REQUIRE(nt::cookCache().stats().stateEntries > 0);
    (void) nm;
}

TEST_CASE_METHOD(CacheReset, "A frame not visited yet is reached from the nearest saved state", "[cookcache]")
{
    const Scene scene = buildScene();

    // The gravity of this simulation is keyed, so the value it holds is different at every frame.
    // The saved states do not depend on it: the solver reads the keys, not the value.
    cookAt(scene.simulation, 50.f);
    const std::uint64_t resumedBefore = nt::cookCache().stats().stateHits;
    cookAt(scene.simulation, 58.f);
    cookAt(scene.simulation, 26.f);
    const std::uint64_t resumedAfter = nt::cookCache().stats().stateHits;
    // Two cooks, and each runs the particles and the spring sheet.
    REQUIRE(resumedAfter == resumedBefore + 4);

    // A cook from nothing finds no state, since the cache is off.
    nt::cookCache().setBudget(0);
    cookAt(scene.simulation, 27.f);
    REQUIRE(nt::cookCache().stats().stateHits == resumedAfter);
}

TEST_CASE_METHOD(CacheReset, "A run resumed from the cache steps only the frames after the saved state", "[cookcache]")
{
    const Scene scene = buildScene();

    // Directly through the solver: a count of the frames each run walks.
    sim::ParticleSettings settings;
    settings.count = 50;
    settings.swirl = 1.f;

    std::map<std::int64_t, std::shared_ptr<const sim::ParticleState>> saved;
    sim::SolverCheckpoints<sim::ParticleState> checkpoints;
    checkpoints.find = [&](std::int64_t maxIndex)
        -> std::optional<std::pair<std::int64_t, std::shared_ptr<const sim::ParticleState>>> {
        auto after = saved.upper_bound(maxIndex);
        if (after == saved.begin()) return std::nullopt;
        --after;
        return std::make_pair(after->first, after->second);
    };
    checkpoints.save = [&](std::int64_t index, const sim::ParticleState& state) {
        saved[index] = std::make_shared<const sim::ParticleState>(state);
    };

    int framesWalked = 0;
    const auto settingsAt = [&](floatT) {
        ++framesWalked;
        return settings;
    };

    const std::vector<sim::Particle> first =
        sim::simulateParticles(settings, settingsAt, 1.f, 101.f, 24.f, 2, &checkpoints);
    REQUIRE(framesWalked == 100);
    REQUIRE(saved.size() == 12);

    // 100 frames further than the last saved state at 96 is four frames, and half of one.
    framesWalked = 0;
    const std::vector<sim::Particle> again =
        sim::simulateParticles(settings, settingsAt, 1.f, 101.f, 24.f, 2, &checkpoints);
    REQUIRE(framesWalked == 4);

    framesWalked = 0;
    const std::vector<sim::Particle> fromNothing =
        sim::simulateParticles(settings, settingsAt, 1.f, 101.f, 24.f, 2, nullptr);
    REQUIRE(framesWalked == 100);

    REQUIRE(first.size() == fromNothing.size());
    REQUIRE(std::memcmp(first.data(), fromNothing.data(), first.size() * sizeof(sim::Particle)) == 0);
    REQUIRE(std::memcmp(again.data(), fromNothing.data(), again.size() * sizeof(sim::Particle)) == 0);

    // A frame in the middle, and one that is not a whole number.
    for (const float end : {9.f, 33.f, 57.5f})
    {
        const auto resumed = sim::simulateParticles(settings, settingsAt, 1.f, end, 24.f, 2, &checkpoints);
        const auto scratch = sim::simulateParticles(settings, settingsAt, 1.f, end, 24.f, 2, nullptr);
        REQUIRE(resumed.size() == scratch.size());
        REQUIRE(std::memcmp(resumed.data(), scratch.data(), resumed.size() * sizeof(sim::Particle)) == 0);
    }

    // The spring sheet resumes the same way.
    sim::SpringSettings springs;
    springs.columns = 6;
    springs.rows = 6;
    std::map<std::int64_t, std::shared_ptr<const sim::SpringState>> savedSprings;
    sim::SolverCheckpoints<sim::SpringState> springCheckpoints;
    springCheckpoints.find = [&](std::int64_t maxIndex)
        -> std::optional<std::pair<std::int64_t, std::shared_ptr<const sim::SpringState>>> {
        auto after = savedSprings.upper_bound(maxIndex);
        if (after == savedSprings.begin()) return std::nullopt;
        --after;
        return std::make_pair(after->first, after->second);
    };
    springCheckpoints.save = [&](std::int64_t index, const sim::SpringState& state) {
        savedSprings[index] = std::make_shared<const sim::SpringState>(state);
    };
    const auto springSettingsAt = [&](floatT) { return springs; };
    sim::simulateSprings(springs, springSettingsAt, 1.f, 81.f, 24.f, 1, &springCheckpoints);
    for (const float end : {10.f, 41.f, 81.f, 65.25f})
    {
        const auto resumed = sim::simulateSprings(springs, springSettingsAt, 1.f, end, 24.f, 1, &springCheckpoints);
        const auto scratch = sim::simulateSprings(springs, springSettingsAt, 1.f, end, 24.f, 1, nullptr);
        REQUIRE(resumed.size() == scratch.size());
        REQUIRE(std::memcmp(resumed.data(), scratch.data(), resumed.size() * sizeof(Vector3)) == 0);
    }
}

TEST_CASE_METHOD(CacheReset, "Anything that changes a simulation loses the states saved for it", "[cookcache]")
{
    const Scene scene = buildScene();

    // What a cook from nothing gives at a frame. It turns the cache off for a moment, which
    // empties it, so it is asked for after the cook that is being checked.
    const auto fromNothing = [&](float frame) {
        nt::cookCache().setBudget(0);
        cookAt(scene.simulation, 1.f);
        cookAt(scene.simulation, frame);
        std::vector<float> result = bytesOf(scene.simulation);
        nt::cookCache().setBudget(nt::CookCache::kDefaultBudget);
        return result;
    };

    // The frame is visited, and states are saved on the way.
    cookAt(scene.simulation, 40.f);
    cookAt(scene.simulation, 2.f);
    REQUIRE(nt::cookCache().stats().stateEntries > 0);

    // A setting changes.
    parameterOf(scene.simulation, "particle_drag")->setFloat(0.2f);
    cookAt(scene.simulation, 40.f);
    const std::vector<float> withDrag = bytesOf(scene.simulation);
    REQUIRE(sameBytes(withDrag, fromNothing(40.f)));

    // A key before the frame that is reached changes. The value at frame 40 does not move, so
    // nothing writes the parameter, and neither the frame nor the states can tell from that.
    cookAt(scene.simulation, 1.f);
    cookAt(scene.simulation, 40.f);
    cookAt(scene.simulation, 1.f);
    REQUIRE(nt::cookCache().stats().stateEntries > 0);
    sim::KeyframeStore::instance().setKeyframe({scene.simulation, "particle_gravity", 1}, 20.f, 5.f);
    cookAt(scene.simulation, 40.f);
    const std::vector<float> withKey = bytesOf(scene.simulation);
    REQUIRE(sameBytes(withKey, fromNothing(40.f)));
    REQUIRE_FALSE(sameBytes(withDrag, withKey));
}

TEST_CASE_METHOD(CacheReset, "Deleting a node drops what it kept", "[cookcache]")
{
    const Scene scene = buildScene();

    for (const float frame : {2.f, 3.f}) cookAt(scene.keyed, frame);
    REQUIRE(nt::cookCache().cachedFrames(scene.keyed).size() == 2);
    const std::size_t before = nt::cookCache().stats().entries;

    nt::nm().deleteNode(scene.keyed);
    REQUIRE(nt::cookCache().cachedFrames(scene.keyed).empty());
    REQUIRE(nt::cookCache().stats().entries < before);
}

TEST_CASE_METHOD(CacheReset, "Clearing the scene empties the cache", "[cookcache]")
{
    const Scene scene = buildScene();
    cookAt(scene.keyed, 3.f);
    REQUIRE(nt::cookCache().stats().entries > 0);

    nt::nm().clear();
    REQUIRE(nt::cookCache().stats().entries == 0);
}

TEST_CASE("The cook cache keeps the most recent entries within its budget on its own", "[cookcache]")
{
    nt::CookCache& cache = nt::cookCache();
    const std::size_t budget = cache.budget();
    cache.clear();
    cache.setBudget(1000);

    const auto cook = [](float frame) {
        auto held = std::make_shared<nt::CachedCook>();
        held->frame = frame;
        return held;
    };
    cache.storeFrame(7, 1, cook(1.f), 400);
    cache.storeFrame(7, 2, cook(2.f), 400);
    REQUIRE(cache.findFrame(7, 1) != nullptr);
    cache.storeFrame(7, 3, cook(3.f), 400);

    // Key 2 was used longest ago.
    REQUIRE(cache.findFrame(7, 2) == nullptr);
    REQUIRE(cache.findFrame(7, 1) != nullptr);
    REQUIRE(cache.findFrame(7, 3) != nullptr);

    // An entry bigger than the whole budget is not kept, and pushes nothing out.
    cache.storeFrame(7, 4, cook(4.f), 5000);
    REQUIRE(cache.findFrame(7, 4) == nullptr);
    REQUIRE(cache.stats().frameEntries == 2);

    // The saved states: the latest at or before an index, and only of the same key.
    cache.clear();
    cache.setBudget(100000);
    cache.storeState(7, 1, 55, 8, std::make_shared<int>(8), 10);
    cache.storeState(7, 1, 55, 16, std::make_shared<int>(16), 10);
    cache.storeState(7, 1, 56, 24, std::make_shared<int>(24), 10);
    cache.storeState(7, 2, 55, 24, std::make_shared<int>(99), 10);
    REQUIRE_FALSE(cache.findState(7, 1, 55, 7).has_value());
    REQUIRE(cache.findState(7, 1, 55, 8)->index == 8);
    REQUIRE(cache.findState(7, 1, 55, 23)->index == 16);
    REQUIRE(cache.findState(7, 1, 55, 1000)->index == 16);
    REQUIRE(*std::static_pointer_cast<const int>(cache.findState(7, 1, 56, 1000)->state) == 24);
    REQUIRE_FALSE(cache.findState(8, 1, 55, 1000).has_value());

    cache.purgeNode(7);
    REQUIRE(cache.stats().entries == 0);

    cache.clear();
    cache.setBudget(budget);
}

TEST_CASE_METHOD(CacheReset, "A table of the spreadsheet is read again when a node is answered from the cache", "[cookcache]")
{
    REQUIRE(core::initialize({}).ok);
    REQUIRE(core::newScene().ok);
    sim::KeyframeStore::instance().clear();
    core::clearCookCache();

    const core::CreateNodeResult grid = core::createNode("helix::grid", "", 0.f, 0.f);
    const core::CreateNodeResult mover = core::createNode("helix::transform", "", 0.f, 0.f);
    REQUIRE(core::connectNodes(grid.id, 0, mover.id, 0).ok);
    REQUIRE(core::setKeyframe(mover.id, "translate", 0, 1.f, 0.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframe(mover.id, "translate", 0, 50.f, 49.f, core::Interpolation::Linear).ok);

    const auto read = [&](float frame) {
        core::setFrame(frame);
        const core::AttributeValues values =
            core::readAttribute(mover.id, core::AttributeOwner::Point, "Position", 0, 8);
        REQUIRE(values.status.ok);
        return std::make_pair(values.generation, values.floats);
    };

    const auto atTen = read(10.f);
    const auto atTwenty = read(20.f);
    const std::uint64_t hitsBefore = core::cookCacheInfo().hits;
    const auto again = read(10.f);

    REQUIRE(core::cookCacheInfo().hits > hitsBefore);
    // The cook that was restored has a number of its own, so a table that holds the values of frame
    // 20 reads them again, and the values are the ones the first visit gave.
    REQUIRE(again.first != atTwenty.first);
    REQUIRE(again.first != atTen.first);
    REQUIRE(again.second == atTen.second);
    REQUIRE(again.second != atTwenty.second);
    REQUIRE(core::nodeCookGeneration(mover.id) == again.first);
}

TEST_CASE_METHOD(CacheReset, "A cook on a worker thread leaves the cache alone, and a cook after it is right", "[cookcache]")
{
    const Scene scene = buildScene();
    auto& nm = nt::nm();

    nm.setAsyncCookingEnabled(true);
    nm.setFrame(7.f);
    nm.requestCook({scene.expression, scene.simulation});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline)
    {
        nm.processPendingCooks();
        if (!nm.isCookBusy() && nm.getPublishedCookResult()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    nm.setAsyncCookingEnabled(false);
    REQUIRE(nm.getPublishedCookResult());

    // A worker reads a snapshot of the network, not the parameters the key is made of.
    REQUIRE(nt::cookCache().stats().entries == 0);

    // What the workers made is what a cook on this thread makes.
    const auto published = nm.getPublishedOutputPacket(scene.expression, 0);
    nm.setFrame(8.f);
    cookAt(scene.expression, 7.f);
    REQUIRE(sameBytes(bytesOf(scene.expression), [&] {
        std::vector<float> out;
        for (const auto& primitive : published->getPrimitives())
        {
            const auto mesh = std::dynamic_pointer_cast<const geo::Mesh>(primitive);
            out.push_back(static_cast<float>(mesh->getNumPoints()));
            out.push_back(static_cast<float>(mesh->getNumFaces()));
            for (Offset point = 0; point < mesh->getNumPoints(); ++point)
            {
                const Vector3 position = mesh->getPointPos(point);
                out.push_back(position.x());
                out.push_back(position.y());
                out.push_back(position.z());
            }
        }
        return out;
    }()));
    REQUIRE(nt::cookCache().stats().entries > 0);
}

TEST_CASE_METHOD(CacheReset, "The facade reports the cache, and clears it", "[cookcache]")
{
    REQUIRE(core::initialize({}).ok);
    REQUIRE(core::newScene().ok);
    sim::KeyframeStore::instance().clear();
    core::setCookCacheBudget(std::uint64_t{1} << 30);
    core::clearCookCache();

    const core::CreateNodeResult grid = core::createNode("helix::grid", "", 0.f, 0.f);
    const core::CreateNodeResult mover = core::createNode("helix::transform", "", 0.f, 0.f);
    REQUIRE(core::connectNodes(grid.id, 0, mover.id, 0).ok);
    REQUIRE(core::setKeyframe(mover.id, "translate", 0, 1.f, 0.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setKeyframe(mover.id, "translate", 0, 50.f, 9.f, core::Interpolation::Linear).ok);
    REQUIRE(core::setDisplayNode(mover.id).ok);

    // Showing the node cooked it at the first frame.
    REQUIRE(core::cachedFrames(mover.id) == std::vector<std::int32_t>{1});
    for (const float frame : {3.f, 4.f, 5.f, 4.f})
    {
        core::setFrame(frame);
        REQUIRE(core::cookDisplayBuffers().status.ok);
    }

    REQUIRE(core::cachedFrames(mover.id) == std::vector<std::int32_t>{1, 3, 4, 5});
    REQUIRE(core::cachedFrames(grid.id).empty());
    REQUIRE(core::cachedFrames(0).empty());

    const core::CookCacheInfo info = core::cookCacheInfo();
    REQUIRE(info.entries == 4);
    REQUIRE(info.frameEntries == 4);
    REQUIRE(info.bytesUsed > 0);
    REQUIRE(info.budget == (std::uint64_t{1} << 30));
    REQUIRE(info.hits >= 1);

    core::clearCookCache();
    REQUIRE(core::cachedFrames(mover.id).empty());
    REQUIRE(core::cookCacheInfo().entries == 0);

    // A smaller budget is reported as set, and zero turns the cache off.
    core::setCookCacheBudget(0);
    REQUIRE(core::cookCacheInfo().budget == 0);
    core::setFrame(6.f);
    REQUIRE(core::cookDisplayBuffers().status.ok);
    REQUIRE(core::cookCacheInfo().entries == 0);
    core::setCookCacheBudget(std::uint64_t{1} << 30);
}

// What the cache is worth, in time. Hidden from the normal run, since a time is not a check:
//   build-core/helix.app/Contents/bin/tests "[.bench]" -r compact   (a Release build)
// Every figure is the cook of one node at one frame, on the thread that plays, and does not
// include the viewport. "cache off" is a budget of zero, which is the engine before the cache.
TEST_CASE_METHOD(CacheReset, "The cost of a frame with and without the cook cache", "[.bench]")
{
    using Clock = std::chrono::steady_clock;
    const auto since = [](Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    };
    auto& nm = nt::nm();
    REQUIRE(nm.getEndFrame() >= 1);
    nm.setEndFrame(1000);

    // Plays frames 1 to 500 one after the other, as playback does, and reports the cost a frame.
    const auto play = [&](nt::NodeId node, const std::string& label) {
        double total = 0.0;
        double lastFifty = 0.0;
        double slowest = 0.0;
        nm.setFrame(1.f);
        nm.cook(node);
        for (int frame = 2; frame <= 500; ++frame)
        {
            const auto start = Clock::now();
            nm.setFrame(static_cast<float>(frame));
            nm.cook(node);
            const double cost = since(start);
            total += cost;
            slowest = std::max(slowest, cost);
            if (frame > 450) lastFifty += cost;
        }
        WARN(label << ": 1 to 500 plays in " << total << " ms, " << total / 499 << " ms a frame on average, "
                   << lastFifty / 50 << " ms a frame over the last fifty, " << slowest << " ms for the slowest");
    };

    // The cost of a visit to a frame that was cooked before, from far away.
    const auto visit = [&](nt::NodeId node, float frame, const std::string& label) {
        nm.setFrame(frame + 100.f);
        nm.cook(node);
        const auto start = Clock::now();
        nm.setFrame(frame);
        nm.cook(node);
        WARN(label << ": going back to frame " << frame << " costs " << since(start) << " ms");
    };

    for (const int count : {64, 20000})
    {
        for (const bool cacheOn : {false, true})
        {
            nm._reset();
            nm.setEndFrame(1000);
            nt::cookCache().clear();
            nt::cookCache().setBudget(cacheOn ? nt::CookCache::kDefaultBudget : 0);

            const nt::NodeId node = addNode("helix::simulation");
            parameterOf(node, "particle_count")->setInt(count);
            const std::string label = std::string("simulation, ") + std::to_string(count) + " particles, cache " +
                                      (cacheOn ? "on " : "off");
            play(node, label);
            visit(node, 100.f, label);
            // A frame that was not cooked on the way: the nearest saved state is the start.
            visit(node, 100.5f, label + " (a frame between two)");
        }
    }

    // A keyed transform on a mesh of a hundred thousand points, played twice.
    for (const bool cacheOn : {false, true})
    {
        nm._reset();
        sim::KeyframeStore::instance().clear();
        nm.setEndFrame(1000);
        nt::cookCache().clear();
        nt::cookCache().setBudget(cacheOn ? nt::CookCache::kDefaultBudget : 0);

        const nt::NodeId grid = addNode("helix::grid");
        parameterOf(grid, "rows")->setInt(317);
        parameterOf(grid, "columns")->setInt(317);
        const nt::NodeId mover = addNode("helix::transform");
        nm.connectNodes(grid, 0, mover, 0);
        auto& store = sim::KeyframeStore::instance();
        store.setKeyframe({mover, "translate", 0}, 1.f, 0.f, sim::Interpolation::Smooth);
        store.setKeyframe({mover, "translate", 0}, 100.f, 20.f, sim::Interpolation::Linear);

        const std::string label = std::string("keyed transform, 100k points, cache ") + (cacheOn ? "on " : "off");
        for (const char* pass : {"first pass", "second pass"})
        {
            nm.setFrame(1.f);
            nm.cook(mover);
            const auto start = Clock::now();
            for (int frame = 2; frame <= 100; ++frame)
            {
                nm.setFrame(static_cast<float>(frame));
                nm.cook(mover);
            }
            WARN(label << ", " << pass << ": " << since(start) / 99 << " ms a frame");
        }
        WARN(label << ": the cache holds " << nt::cookCache().stats().bytesUsed / (1024 * 1024) << " MiB");
    }
}
