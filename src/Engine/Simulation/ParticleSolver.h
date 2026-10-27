#pragma once
#include "Engine/Core/Types.h"
#include <functional>
#include <vector>

namespace helix::sim {

/// @brief One particle of a particle system.
struct Particle
{
    Vector3 position{0.f, 0.f, 0.f};
    Vector3 velocity{0.f, 0.f, 0.f};
};

/// @brief Everything that shapes a particle system.
struct ParticleSettings
{
    /// @brief How many particles the system holds.
    intT count = 64;

    /// @brief Where the particles start.
    Vector3 origin{0.f, 0.f, 0.f};

    /// @brief The size of the box the particles start in, on each axis.
    Vector3 extent{1.f, 1.f, 1.f};

    /// @brief The pull the particles feel, on each axis.
    Vector3 gravity{0.f, -9.81f, 0.f};

    /// @brief The fraction of a particle's speed that is lost every step.
    floatT drag = 0.02f;

    /// @brief How hard the vortex around the origin pushes the particles.
    floatT swirl = 0.f;

    /// @brief The seed the starting positions are worked out from.
    intT seed = 1;
};

/**
 * @brief A particle system that moves forward one step at a time.
 *
 * The solver is stepped by hand, so the same steps in the same order always
 * give the same answer. Nothing in it reads a clock or a random source.
 *
 * Example
 *
 * ```
 * helix::sim::ParticleSolver solver;
 * solver.reset(settings);
 * solver.step(settings, 1.f / 24.f);
 * ```
 */
class ParticleSolver
{
  public:
    /// @brief Puts the particles back to where they start.
    void reset(const ParticleSettings& settings);

    /// @brief Moves the particles forward by one step.
    void step(const ParticleSettings& settings, floatT deltaTime);

    /// @brief The particles as they stand now.
    const std::vector<Particle>& getParticles() const { return particles_; }

    /// @brief How many particles the solver holds.
    std::size_t size() const { return particles_.size(); }

  private:
    std::vector<Particle> particles_;
};

/**
 * @brief Runs a particle system from one frame to another.
 *
 * The system is always worked out from the frame it starts on, never from
 * whatever the last run left behind, so the state at a frame is the same
 * whether the scene was played to it or scrubbed to it.
 *
 * @param startSettings The settings the system starts with.
 * @param settingsAtFrame Returns the settings a frame runs with, so a setting
 * that is keyframed changes as the system moves.
 * @param startFrame The frame the system starts on.
 * @param endFrame The frame the system ends on.
 * @param fps How many frames make up a second.
 * @param subSteps How many steps each frame is broken into.
 * @return The particles as they stand at the end frame.
 */
std::vector<Particle> simulateParticles(
    const ParticleSettings& startSettings,
    const std::function<ParticleSettings(floatT frame)>& settingsAtFrame,
    floatT startFrame,
    floatT endFrame,
    floatT fps,
    intT subSteps = 1
);

} // namespace helix::sim
