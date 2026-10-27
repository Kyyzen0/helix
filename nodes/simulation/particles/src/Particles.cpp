#include "Engine/Core/Types.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/NodeImpl.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Network/NodeRegistry.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Simulation/SimulationSettings.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace {

using helix::floatT;
using helix::intT;

/**
 * @brief A particle system that runs over time.
 *
 * The particles are always worked out from the frame the node starts on, so
 * the state at a frame is the same whether the scene was played to it or
 * scrubbed to it.
 *
 * Every setting can be keyframed from the timeline. A setting that holds
 * keyframes is read at each step of the simulation, so the system moves with
 * the animation rather than jumping when the frame changes.
 */
class Particles : public helix::nt::NodeImpl
{
  public:
    using NodeImpl::NodeImpl;

    void cook() override;
};

void Particles::cook()
{
    using namespace helix;

    // Reading the frame tells the engine the node depends on the time, so it
    // cooks again whenever the scene moves.
    const floatT frame = getFrame();

    const intT startFrame = std::max<intT>(1, evalParmInt("start_frame"));
    const intT subSteps = std::max<intT>(1, evalParmInt("sub_steps"));
    const floatT fps = nt::nm().getFps();
    const floatT firstFrame = static_cast<floatT>(startFrame);

    if (!outputRequested(0)) return;

    NodePacket packet;
    auto mesh = std::make_shared<geo::Mesh>();

    const auto settingsAtFrame = [this](floatT atFrame) {
        return sim::particleSettingsAt(getNodeId(), atFrame);
    };

    const std::vector<sim::Particle> particles = sim::simulateParticles(
        sim::particleSettingsAt(getNodeId(), firstFrame),
        settingsAtFrame,
        firstFrame,
        frame,
        fps,
        subSteps
    );

    std::vector<Vector3> positions;
    positions.reserve(particles.size());
    for (const sim::Particle& particle : particles)
        positions.push_back(particle.position);

    mesh->addPoints(positions);
    packet.addPrimitive(std::move(mesh));
    setOutputPacket(0, packet);
}

} // namespace

HELIX_REGISTER_NODE(particles, Particles)
