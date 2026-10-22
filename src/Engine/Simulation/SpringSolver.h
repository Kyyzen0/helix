#pragma once
#include "Engine/Core/Types.h"
#include <functional>
#include <vector>

namespace enzo::sim {

/// @brief Everything that shapes a spring lattice.
struct SpringSettings
{
    /// @brief How many points the lattice has along its width.
    intT columns = 8;

    /// @brief How many points the lattice has down its height.
    intT rows = 8;

    /// @brief How far apart the points sit when the lattice is at rest.
    floatT spacing = 1.f;

    /// @brief Where the lattice hangs from.
    Vector3 origin{0.f, 0.f, 0.f};

    /// @brief The pull the lattice feels, on each axis.
    Vector3 gravity{0.f, -9.81f, 0.f};

    /// @brief How hard a spring pulls its two ends back together, from 0 to 1.
    floatT stiffness = 0.5f;

    /// @brief How much of the movement a step keeps, from 0 to 1.
    floatT damping = 0.98f;

    /// @brief How many times the springs are pulled back together in a step.
    intT iterations = 4;

    /// @brief The seed the lattice starts from.
    intT seed = 1;
};

/**
 * @brief A hanging lattice of points joined by springs.
 *
 * The solver is stepped by hand, so the same steps in the same order always
 * give the same answer. Nothing in it reads a clock or a random source.
 *
 * Example
 *
 * ```
 * enzo::sim::SpringSolver solver;
 * solver.reset(settings);
 * solver.step(settings, 1.f / 24.f);
 * ```
 */
class SpringSolver
{
  public:
    /// @brief Puts the lattice back to the shape it hangs in when at rest.
    void reset(const SpringSettings& settings);

    /// @brief Moves the lattice forward by one step.
    void step(const SpringSettings& settings, floatT deltaTime);

    /// @brief The points of the lattice as they stand now.
    const std::vector<Vector3>& getPositions() const { return positions_; }

    /// @brief How many points the lattice holds.
    std::size_t size() const { return positions_.size(); }

    /// @brief The index of the point at a column and a row.
    intT indexAt(intT column, intT row) const { return row * settings_.columns + column; }

  private:
    /// @brief Pulls the two ends of every spring back together.
    void satisfySprings_(const SpringSettings& settings);

    /// @brief The rest length of the spring between two points.
    floatT restLength_(intT fromIndex, intT toIndex) const;

    std::vector<Vector3> positions_;

    // Where the points were before the last step, which is what the Verlet
    // step works the new positions out from.
    std::vector<Vector3> previousPositions_;

    // The two ends of every spring, as indexes into the points.
    std::vector<std::pair<intT, intT>> springs_;

    // The length every spring settles back to.
    std::vector<floatT> restLengths_;

    // The settings the lattice was last built from.
    SpringSettings settings_;
};

/**
 * @brief Runs a spring lattice from one frame to another.
 *
 * The lattice is always worked out from the frame it starts on, never from
 * whatever the last run left behind, so the state at a frame is the same
 * whether the scene was played to it or scrubbed to it.
 *
 * @param startSettings The settings the lattice starts with.
 * @param settingsAtFrame Returns the settings a frame runs with, so a setting
 * that is keyframed changes as the lattice moves.
 * @param startFrame The frame the lattice starts on.
 * @param endFrame The frame the lattice ends on.
 * @param fps How many frames make up a second.
 * @param subSteps How many steps each frame is broken into.
 * @return The points of the lattice as they stand at the end frame.
 */
std::vector<Vector3> simulateSprings(
    const SpringSettings& startSettings,
    const std::function<SpringSettings(floatT frame)>& settingsAtFrame,
    floatT startFrame,
    floatT endFrame,
    floatT fps,
    intT subSteps = 1
);

} // namespace enzo::sim
