#include "Engine/Core/Types.h"
#include "Engine/Network/NetworkManager.h"
#include "Engine/Network/NodeImpl.h"
#include "Engine/Network/NodePacket.h"
#include "Engine/Network/NodeRegistry.h"
#include "Engine/Primitives/Mesh.h"
#include "Engine/Simulation/KeyframeStore.h"
#include "Engine/Simulation/ParticleSolver.h"
#include "Engine/Simulation/SpringSolver.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace {

using helix::floatT;
using helix::intT;

/**
 * @brief A node that runs two procedural simulations over time.
 *
 * The first output holds the particles of a particle system, and the second
 * holds the points of a hanging spring sheet. Both are worked out from the
 * frame the node starts on, so the state at a frame is the same whether the
 * scene was played to it or scrubbed to it.
 *
 * Every setting can be keyframed from the timeline. A setting that holds
 * keyframes is read at each step of the simulation, so the system moves with
 * the animation rather than jumping when the frame changes.
 */
class Simulation : public helix::nt::NodeImpl
{
  public:
    using NodeImpl::NodeImpl;

    void cook() override;

  private:
    /// @brief Returns the value of a parameter at a frame, keyframes included.
    floatT valueAt_(const char* parameter, unsigned int component, floatT frame) const;

    /// @brief The settings of the particle system at a frame.
    helix::sim::ParticleSettings particleSettingsAt_(floatT frame) const;

    /// @brief The settings of the spring sheet at a frame.
    helix::sim::SpringSettings springSettingsAt_(floatT frame) const;

    /// @brief Where the particle system resumes from and what it leaves behind.
    helix::sim::SolverCheckpoints<helix::sim::ParticleState> particleCheckpoints_() const;

    /// @brief Where the spring sheet resumes from and what it leaves behind.
    helix::sim::SolverCheckpoints<helix::sim::SpringState> springCheckpoints_() const;
};

floatT Simulation::valueAt_(const char* parameter, unsigned int component, floatT frame) const
{
    return helix::sim::resolveParameter(getNodeId(), parameter, component, frame);
}

helix::sim::ParticleSettings Simulation::particleSettingsAt_(floatT frame) const
{
    helix::sim::ParticleSettings settings;
    settings.count = static_cast<helix::intT>(valueAt_("particle_count", 0, frame));
    settings.origin = helix::Vector3(
        valueAt_("particle_origin", 0, frame),
        valueAt_("particle_origin", 1, frame),
        valueAt_("particle_origin", 2, frame)
    );
    settings.extent = helix::Vector3(
        valueAt_("particle_extent", 0, frame),
        valueAt_("particle_extent", 1, frame),
        valueAt_("particle_extent", 2, frame)
    );
    settings.gravity = helix::Vector3(
        valueAt_("particle_gravity", 0, frame),
        valueAt_("particle_gravity", 1, frame),
        valueAt_("particle_gravity", 2, frame)
    );
    settings.drag = valueAt_("particle_drag", 0, frame);
    settings.swirl = valueAt_("particle_swirl", 0, frame);
    settings.seed = static_cast<helix::intT>(valueAt_("particle_seed", 0, frame));
    return settings;
}

helix::sim::SpringSettings Simulation::springSettingsAt_(floatT frame) const
{
    helix::sim::SpringSettings settings;
    settings.columns = static_cast<helix::intT>(valueAt_("spring_columns", 0, frame));
    settings.rows = static_cast<helix::intT>(valueAt_("spring_rows", 0, frame));
    settings.spacing = valueAt_("spring_spacing", 0, frame);
    settings.origin = helix::Vector3(
        valueAt_("spring_origin", 0, frame),
        valueAt_("spring_origin", 1, frame),
        valueAt_("spring_origin", 2, frame)
    );
    settings.gravity = helix::Vector3(
        valueAt_("spring_gravity", 0, frame),
        valueAt_("spring_gravity", 1, frame),
        valueAt_("spring_gravity", 2, frame)
    );
    settings.stiffness = valueAt_("spring_stiffness", 0, frame);
    settings.damping = valueAt_("spring_damping", 0, frame);
    settings.iterations = static_cast<helix::intT>(valueAt_("spring_iterations", 0, frame));
    settings.seed = static_cast<helix::intT>(valueAt_("spring_seed", 0, frame));
    return settings;
}

helix::sim::SolverCheckpoints<helix::sim::ParticleState> Simulation::particleCheckpoints_() const
{
    using namespace helix;

    // The states live in the cook cache, under everything this cook depends on except the frame,
    // so a state is only reused by a cook that would have reached it by itself.
    sim::SolverCheckpoints<sim::ParticleState> checkpoints;
    checkpoints.find = [this](std::int64_t maxIndex)
        -> std::optional<std::pair<std::int64_t, std::shared_ptr<const sim::ParticleState>>> {
        const std::optional<nt::CachedState> found = findSolverState(0, maxIndex);
        if (!found) return std::nullopt;
        return std::make_pair(
            found->index,
            std::static_pointer_cast<const sim::ParticleState>(found->state)
        );
    };
    checkpoints.save = [this](std::int64_t index, const sim::ParticleState& state) {
        saveSolverState(
            0,
            index,
            std::make_shared<const sim::ParticleState>(state),
            state.size() * sizeof(sim::Particle) + sizeof(sim::ParticleState)
        );
    };
    return checkpoints;
}

helix::sim::SolverCheckpoints<helix::sim::SpringState> Simulation::springCheckpoints_() const
{
    using namespace helix;

    sim::SolverCheckpoints<sim::SpringState> checkpoints;
    checkpoints.find = [this](std::int64_t maxIndex)
        -> std::optional<std::pair<std::int64_t, std::shared_ptr<const sim::SpringState>>> {
        const std::optional<nt::CachedState> found = findSolverState(1, maxIndex);
        if (!found) return std::nullopt;
        return std::make_pair(
            found->index,
            std::static_pointer_cast<const sim::SpringState>(found->state)
        );
    };
    checkpoints.save = [this](std::int64_t index, const sim::SpringState& state) {
        saveSolverState(
            1,
            index,
            std::make_shared<const sim::SpringState>(state),
            (state.positions.size() + state.previousPositions.size()) * sizeof(Vector3) +
                sizeof(sim::SpringState)
        );
    };
    return checkpoints;
}

void Simulation::cook()
{
    using namespace helix;

    // Reading the frame tells the engine the node depends on the time, so it
    // cooks again whenever the scene moves.
    const floatT frame = getFrame();

    const intT startFrame = static_cast<intT>(evalParmInt("start_frame"));
    const intT subSteps = std::max<intT>(1, evalParmInt("sub_steps"));
    const floatT fps = nt::nm().getFps();
    const floatT firstFrame = static_cast<floatT>(startFrame);

    if (outputRequested(0))
    {
        NodePacket packet;
        auto mesh = std::make_shared<geo::Mesh>();

        const auto settingsAtFrame = [this](floatT atFrame) {
            return particleSettingsAt_(atFrame);
        };

        const sim::SolverCheckpoints<sim::ParticleState> checkpoints = particleCheckpoints_();
        const std::vector<sim::Particle> particles = sim::simulateParticles(
            particleSettingsAt_(firstFrame),
            settingsAtFrame,
            firstFrame,
            frame,
            fps,
            subSteps,
            &checkpoints
        );

        std::vector<Vector3> positions;
        positions.reserve(particles.size());
        for (const sim::Particle& particle : particles)
            positions.push_back(particle.position);

        mesh->addPoints(positions);
        packet.addPrimitive(std::move(mesh));
        setOutputPacket(0, packet);
    }

    if (outputRequested(1))
    {
        NodePacket packet;
        auto mesh = std::make_shared<geo::Mesh>();

        const auto settingsAtFrame = [this](floatT atFrame) {
            return springSettingsAt_(atFrame);
        };

        const sim::SolverCheckpoints<sim::SpringState> checkpoints = springCheckpoints_();
        const std::vector<Vector3> positions = sim::simulateSprings(
            springSettingsAt_(firstFrame),
            settingsAtFrame,
            firstFrame,
            frame,
            fps,
            subSteps,
            &checkpoints
        );

        const std::vector<Offset> offsets = mesh->addPoints(positions);

        // The sheet is drawn as a grid of faces, so it covers the gaps between
        // its points.
        const intT columns = std::max<intT>(1, evalParmInt("spring_columns"));
        const intT rows = std::max<intT>(1, evalParmInt("spring_rows"));
        if (static_cast<intT>(offsets.size()) >= columns * rows)
        {
            for (intT row = 0; row + 1 < rows; ++row)
            {
                for (intT column = 0; column + 1 < columns; ++column)
                {
                    const Offset topLeft = offsets[static_cast<std::size_t>(row * columns + column)];
                    const Offset topRight = offsets[static_cast<std::size_t>(row * columns + column + 1)];
                    const Offset bottomLeft =
                        offsets[static_cast<std::size_t>((row + 1) * columns + column)];
                    const Offset bottomRight =
                        offsets[static_cast<std::size_t>((row + 1) * columns + column + 1)];
                    mesh->addFace({topLeft, topRight, bottomRight, bottomLeft}, true);
                }
            }
        }

        packet.addPrimitive(std::move(mesh));
        setOutputPacket(1, packet);
    }
}

} // namespace

HELIX_REGISTER_NODE(simulation, Simulation)
